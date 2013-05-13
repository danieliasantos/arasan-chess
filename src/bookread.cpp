// Copyright 1993-1999, 2005, 2009, 2012, 2013 by Jon Dart.d
// All Rights Reserved.
#ifdef _MSC_VER
#pragma optimize("g", off)
#endif

#include "bookread.h"
#include "constant.h"
#include "bookdefs.h"
#include "movegen.h"
#include "globals.h"
#include "util.h"
#include "debug.h"
#include <string.h>
#include <iostream> // for debugging
#include <assert.h>
#ifdef _WIN32
  #include <windows.h>
#endif

BookReader::BookReader(const char* fileName, const char* mappingName,
 bool create)
: book_file(NULL),
pBook(NULL),
pPage(NULL),
hFileMapping(NULL),
open(0),
current_page(-1)
{                 
   string book_path = derivePath(fileName);
   book_file = bu.openFile(book_path.c_str(),false);
   if (book_file == NULL) {
      cerr << "Warning: could not open book file" << endl;
      return;
   }
   if (book_file != NULL) {
      hFileMapping = bu.createMap(book_file,false);
      if (hFileMapping == NULL) {
#ifdef _WIN32
         ::MessageBox(NULL,"Error: could not create file mapping for opening book","Error",MB_OK | MB_ICONEXCLAMATION);
#else
	 cerr << "warning: file mapping failed for opening book" << endl;
#endif
      }
   }
   if (hFileMapping) {
      pBook = bu.map(hFileMapping,0,4096,false);
      if (pBook == NULL) {
#ifdef _WIN32
         ::MessageBox(NULL,"Error: file mapping failed for opening book","Error",MB_OK | MB_ICONEXCLAMATION);
#else 
	 cerr << "warning: file mapping failed for opening book" << endl;
#endif
      }
   }

   open = pBook != NULL;
   if (!open)
      return;
   cout << "Using book file " << fileName << endl;

   char *p = (char*)pBook;
   hdr.version = (byte)(*p);
   hdr.num_pages = (byte)p[1];
   // book file is little endian, convert if needed;
   hdr.page_capacity = swapEndian32((const byte*)(p+2));
   hdr.hash_table_size = swapEndian32((const byte*)(p+6));

   if (hdr.version != (byte)BookVersion)
   {
      char msg[128];
      sprintf(msg,"Expected book version %d, got %d instead",
              (int)BookVersion,(int)hdr.version);
#ifdef _WIN32
      MessageBox(NULL,msg,"Warning",MB_OK | MB_ICONEXCLAMATION);
#else
      fprintf(stderr,"Warning: %s\n",msg);
#endif
      bu.unmap(pBook,hdr.page_capacity);
      bu.closeMap(hFileMapping);
      bu.closeFile(book_file);
      open = 0;
      return;
   }
   bu.unmap(pBook,4096);
   pBook = NULL;
}

BookReader::~BookReader()
{
   if (pBook)
      bu.unmap(pBook,hdr.page_capacity);
   if (hFileMapping)
      bu.closeMap(hFileMapping);
   if (book_file)
      bu.closeFile(book_file);
}

void BookReader::syncCurrentPage() {
    bu.sync(pBook,hdr.page_capacity);
}

void BookReader::head( const Board & b, BookLocation &loc)
{
   loc.page = (int)(b.hashCode() % hdr.num_pages);
   loc.index = INVALID;
   // Don't return a book move if we have repeated this position
   // before .. make the program to search to see if the repetition
   // is desirable or not.
   if (b.repCount() > 0) {
     return;
   }
   fetch_page(loc.page);
   unsigned probe = (unsigned)((b.hashCode()>>32) % hdr.hash_table_size);

   // Copy the index from the book
   loc.index = swapEndian16(pPage+Header_Size+sizeof(uint16)*probe);
}

void BookReader::head( hash_t hashcode,
                       BookLocation &loc)
{
   loc.page = (int)(hashcode % hdr.num_pages);
   loc.index = INVALID;
   fetch_page(loc.page);
   unsigned probe = (unsigned)((hashcode>>32) % hdr.hash_table_size);

   // Copy the index from the book
   loc.index = swapEndian16(pPage+Header_Size+sizeof(uint16)*probe);
}

void BookReader::fetch( const BookLocation &loc, BookInfo &book_info )
{
   assert(pPage);

   byte *entry = pPage + Header_Size + hdr.hash_table_size*2 + ((int)loc.index)*Entry_Size;
   hash_t hc;
   byte freq, indx;

   // We assume here that the compiler doesn't reorder structures

   hc = swapEndian64(entry);
   freq = entry[8];
   indx = entry[9];

   byte flags = entry[10];
   book_info.init(hc,freq,flags,indx);
   book_info.winloss = entry[11];
   book_info.setLocation(loc);
   // convert float byte order if necessary
   book_info.learn_score = swapEndianFloat(entry+12);
   book_info.flags2 = entry[16];
}

void BookReader::update(const BookLocation &loc, float learn_factor)
{
   fetch_page(loc.page);
   byte *entry = pPage + Header_Size + hdr.hash_table_size*2 + ((int)loc.index)*Entry_Size;
   // get the existing learn factor
   float old_factor = swapEndianFloat(entry+12);
   // update it
   learn_factor += old_factor;
   // write to book image in memory
   float *e = (float*)(entry+12);
   *e = swapEndianFloat((byte*)&learn_factor);
   syncCurrentPage();
}

void BookReader::update(const BookLocation &loc,BookEntry *newEntry) {
   fetch_page(loc.page);
   byte *entry = pPage + Header_Size + hdr.hash_table_size*2 + ((int)loc.index)*Entry_Size;
   entry[16] = newEntry->flags2;
   syncCurrentPage();
}


// Determine the weighting a book move will receive. Moves with higher weights
// will be played more often.
//
static int get_weight(const BookInfo &be,int total_freq,const ColorType side)
{
   int rec = be.get_recommend();
#ifdef _TRACE
   cout << "index = " << (int)be.get_move_index() << " rec= " << rec;
#endif
   if (rec == 0 && !options.book.random) {
#ifdef _TRACE
      cout << endl;
#endif
      return 0;
   }
   int w = be.get_winloss();
   // Moves that have led to nothing but losses get zero weight,
   // unless the "random" option is on:
   if (w == -100 && !options.book.random)
      return 0;
   const int freq = be.get_frequency();
   // If strength reduction is enabled, "dumb down" the opening book
   // by pruning away infrequent moves.
   if (options.search.strength < 100 && freq <
       1<<((100-options.search.strength)/10)) {
       return 0;
   }
   int freqWeight = (int)((100L*freq)/total_freq);
   int winWeight = (w+100)/2;
#ifdef _TRACE
     cout << " freqWeight=" << freqWeight << " winWeight=" << winWeight << endl;
#endif
   // fold in result learning. 
   if (options.learning.result_learning) {
     int w2 = be.get_learned_result();
     if (w2 < 0) w2 = Util::Max(w2,-20);
     if (w2 > 0) w2 = Util::Min(w2,20);
     winWeight = (winWeight + (w2*8));
   }
   if (winWeight < 0) winWeight = 0;
   if (winWeight > 100) winWeight = 100;
   int base;
   // Favor more frequent moves and moves that win
   if (rec != 50)
      base = rec;
   else
      base = (freqWeight*winWeight)/40;
   base = Util::Min(100,base);
   if (base == 0) return 0;
   // Factor in score-based learning
   if (options.learning.score_learning) {
     int score = (int)be.learn_score;
      if (score >= 0)
         return Util::Min(100,(base*(10+score))/10);
      else
         return Util::Max(0,(base*(10+score))/10);
   }
   else
     return base;
}

Move BookReader::pick( const Board &b, const BookLocation &loc,
                        BookInfo &info )
{
#ifdef _TRACE
  cout << "BookReader::pick - hash=" << (hex) << b.hashCode() << (dec) << endl;
#endif
   BookLocation tmp = loc;
   BookLocation locs[100];
   int total_freq = 0;
   int count = 0;
   // get the total move frequency (needed for computing weights)
   while (tmp.index != INVALID) {
      BookInfo be;
      fetch(tmp, be);
      if (be.hash_code() == b.hashCode())
      {
          if (be.get_recommend() != 0) {
             total_freq += be.get_frequency();
             ++count;
          }
      }
      if (be.is_last())
         break;
      else
         tmp.index++;
   }
   tmp = loc;
   // Determine the total weights of moves for this position,
   // and build a list of candidate moves.
   //
   BookEntry candidates[100], candidates2[100];
   int candidate_weights[100], candidate_weights2[100];
   int candidate_count = 0; 
   unsigned total_weight = 0;
   int max_weight = 0;
   while (tmp.index != INVALID) {
      BookInfo be;
      fetch(tmp, be);

      if (b.hashCode() == be.hash_code())
      {
         int w  = get_weight(be,total_freq,b.sideToMove());
#ifdef _TRACE
         cout << "index = " << (int)tmp.index << " weight = " << w << " move index = " << (int)be.move_index <<
         " is_basic = " << be.is_basic() << endl;
#endif
         if (w>max_weight) max_weight=w;
         total_weight += w;
         if (w > 0) {
            candidate_weights[candidate_count] = w;
            candidates[candidate_count++] = be;
	 }
       }
       if (be.is_last())
          break;
       else
         tmp.index++;
   }
#ifdef _TRACE
   cout << "candidate_count = " << candidate_count << endl;
#endif   

   // modify the weights for the non-best move based on selectivity setting
   for (int i = 0; i < candidate_count; i++) {
      int w  = candidate_weights[i];
      if (w != max_weight) {
#ifdef _TRACE
          cout << "orig weight = " << w << endl;
#endif
          w = int(w*(1.0 + 0.0125*(50-options.book.selectivity)));
          int factor = 25-options.book.selectivity;
          if (w !=0 && factor > 0) {
              w += max_weight*(factor*factor)/625;
          }
          w = Util::Min(max_weight,w);
          candidate_weights[i] = w;
#ifdef _TRACE
          cout << "modified weight = " << w << endl;
#endif
      }
   }

   // Depending on the selectivity value selected, remove moves
   // from the candidate list.
   int candidate_count2 = 0;
   // compute minimum weight we will accept
   int min_weight = (options.book.selectivity*max_weight)/400;
#ifdef _TRACE
   cout << "min_weight = " << min_weight << endl;
#endif
   int i;
   total_weight = 0;
   for (i = 0; i < candidate_count; i++) {
      int w  = candidate_weights[i];
#ifdef _TRACE
      cout << " w = " << w << " index=" << (int)candidates[i].move_index << endl;
#endif
      if (w >= min_weight) {
         total_weight += w;
         candidate_weights2[candidate_count2] = w;
         candidates2[candidate_count2] = candidates[i];
         locs[candidate_count2++] = tmp;
      }
   }
#ifdef _TRACE
   cout << "candidate_count2 = " << candidate_count2 << " total_weight = " << total_weight << endl;
#endif   
   return pickRandom(b,candidates2,candidate_weights2,candidate_count2,total_weight,locs,info);
}

Move BookReader::pickRandom(const Board &b, BookEntry * candidates,int * candidate_weights,
  int candidate_count,int total_weight,BookLocation *locs,BookInfo &info)
{
   // If total_weight is 0, no moves have non-zero weights.
   if (total_weight == 0) return NullMove;

   const unsigned nRand = rand() % total_weight;
   // Randomly pick from the available moves.  Prefer moves
   // with high weights.
#ifdef _TRACE
   cout << "nRand = " << nRand << endl; 
#endif
   unsigned weight = 0;
   for (int i=0; i < candidate_count; i++) 
   {
      int w = candidate_weights[i];
      weight += w;
      if (nRand <= weight)
      {
         // We have selected a move. The book contains
         // only the move index, not the move itself.
         // We must call the move generator to obtain the
         // move.
#ifdef _TRACE
        cout << "selecting " << (int) candidates[i].move_index << endl;
#endif
         Move moves[Constants::MaxMoves];
         MoveGenerator mg(b);
         int n = mg.generateAllMoves(moves,1 /*repeatable*/); 
#ifdef _TRACE
	 cout << " index=" << (int)candidates[i].move_index << 
	   " n=" << n << endl;
	 MoveImage(moves[candidates[i].move_index],cout); cout << endl;
#endif
         assert(candidates[i].move_index < n);
         // Copy selected book entry into info param - this is used
         // if we need to update the entry due to book learning.
         info.setMove(moves[candidates[i].move_index]);
         info.setTotalMoves(candidate_count);
         info.setLocation(locs[i]);
	 info.set_hash_code(candidates[i].hash_code());
         return moves[candidates[i].move_index];
      }
   }
   // should never get here
   assert(0);
   return NullMove;
}

int BookReader::book_moves(const Board &b, Move *moves, int *scores, const unsigned limit)
{
   BookLocation tmp;
   head(b,tmp);
   BookEntry target(b.hashCode(),0,0,0);
   int num_moves = 0;
   int total_freq = 0;
   Move *tmp_moves = new Move[Constants::MaxMoves];
   MoveGenerator mg( b, NULL, 0, NullMove, 0);
   int n = mg.generateAllMoves(tmp_moves,1 /* repeatable */); 
   // get the total move frequency
   while (tmp.index != INVALID)
   {
      BookInfo be;
      fetch(tmp,be);
      if (target == be && be.get_recommend())
      {
         num_moves++;
         total_freq += be.get_frequency();
      }
      if (be.is_last())
         break;
      else
         tmp.index++;
   }
   if (num_moves == 0)
      return 0;
   Move * tmp_moves2 = new Move[num_moves];
   BookLocation loc;
   head(b,loc);
   num_moves = 0;
   tmp = loc;
   while (tmp.index != INVALID)
   {
      BookInfo be;
      fetch(tmp,be);
      int w;
      if (target == be && (w = get_weight(be,total_freq,b.sideToMove())) != 0)
      {
         assert(be.move_index < n);
         tmp_moves2[num_moves] = tmp_moves[be.move_index];
         scores[num_moves++] = w;
      }
#ifdef _TRACE
      if (target == be) {
	MoveImage(tmp_moves[be.get_move_index()],cout);
        cout << " index = " << (int)be.get_move_index() <<
	" winloss = " << (int)be.get_winloss() <<
        " freq = " << (int)be.get_frequency()  <<
	  " recommend = " << (int)be.get_recommend() << " is_basic=" << be.is_basic() << 
          " weight = " << get_weight(be,total_freq,b.sideToMove()) << 
          endl; 
      }
#endif
      if (be.is_last())
         break;
      else
         tmp.index++;
   }
   MoveGenerator::sortMoves(tmp_moves2,scores,num_moves);
   unsigned ret_val = Util::Min(num_moves,limit);
   for (unsigned i = 0; i < ret_val; i++)
      moves[i] = tmp_moves2[i];
   delete [] tmp_moves;
   delete [] tmp_moves2;
   return ret_val;
}

int BookReader::book_move_count(hash_t hashcode) {
   BookLocation tmp;
   head(hashcode,tmp);
   BookEntry target(hashcode,0,0,0);
   int num_moves = 0;
   int total_freq = 0;
   // get the total move frequency
   while (tmp.index != INVALID)
   {
      BookInfo be;
      fetch(tmp,be);
      if (target == be && be.get_recommend())
      {
         num_moves++;
         total_freq += be.get_frequency();
      }
      if (be.is_last())
         break;
      else
         tmp.index++;
   }
   if (num_moves == 0)
      return 0;
   BookLocation loc;
   head(hashcode,loc);
   num_moves = 0;
   tmp = loc;
   ColorType side;
   if (hashcode & (hash_t)1)
     side = Black;
   else
     side = White;
   while (tmp.index != INVALID)
   {
      BookInfo be;
      fetch(tmp,be);
      int w;
             
      if (target == be && (w = get_weight(be,total_freq,side)) != 0)
      {
	num_moves++;
      }
      if (be.is_last())
         break;
      else
         tmp.index++;
   }
   return num_moves;
}

void BookReader::fetch_page(int page)
{
   if (current_page != page)
   {
      if (pBook)
      {
         bu.unmap(pBook,hdr.page_capacity);
      }
      pBook = bu.map(hFileMapping,
                      page*hdr.page_capacity,hdr.page_capacity,false);
      if (!pBook)
      {
#ifdef _WIN32
         MessageBox( NULL, "File mapping failed for opening book", "Error", MB_ICONEXCLAMATION);
#endif
         pPage = NULL;
         return;
      }
      pPage = (byte*)pBook;
   }
   current_page = page;
}