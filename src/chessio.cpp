// Copyright 1994, 1995, 2008, 2012, 2013  by Jon Dart.

#include "chessio.h"
#include "epdrec.h"
#include "debug.h"
#include "constant.h"

#include <sstream>
using namespace std;

#ifdef _WIN32
#include <time.h>
#endif

using namespace std;

#define PGN_MARGIN 70
#define MAX_TAG 255
#define MAX_VAL 255

// This module handles reading and writing board and
// game info.

static int skip_space(istream &game_file)
{
    int c = EOF;
    while (!game_file.eof()) {
       c = game_file.get();
       if (!isspace(c) && (c!='\n'))
       {
           break;
       }
    }
    return c;
}

void ChessIO::get_game_description(const ArasanVector<Header> &hdrs, string &descr, long id)
{
      stringstream s;
      string tmp;
      get_header(hdrs, "White", tmp);
      string::size_type comma = tmp.find(',',0);
      if (comma != string::npos)
      {
          tmp = tmp.substr(0,comma);
      }
      s << tmp << '-';
      get_header(hdrs, "Black", tmp);
      comma = tmp.find(',',0);
      if (comma != string::npos)
      {
          tmp = tmp.substr(0,comma);
      }
      s << tmp;
      get_header(hdrs, "Site", tmp);
      s << ", " << tmp;
      get_header(hdrs, "Round", tmp);
      if (tmp.length() >0 && tmp[0] != '?')
      {
        s << '(' << tmp << ')';
      }
      s << ' ';
      get_header(hdrs, "Date", tmp);
      s << tmp.substr(0,4);
      get_header(hdrs, "Result", tmp);
      if (tmp != "*") 
      {
        s << ' ' << tmp;
      }
      s << '\t' << id;
      descr = s.str();
}

int ChessIO::scan_pgn(istream &game_file, ArasanVector<string> &contents)
{
    Board board;
    int c;

    while (game_file.good() && !game_file.eof())
    {
        // Collect the header:
        long first;
        ArasanVector<Header> hdrs;
        string eventStr;
        collect_headers(game_file, hdrs, first);
        if (get_header(hdrs,"Event",eventStr))
        {
           // We have the headers, munge them into a single-line game
           // description. Append the index to the game so the GUI
           // can navigate to the game when the user clicks on the 
           // description.
           string descr;
           get_game_description(hdrs, descr, first);
           contents.append(descr);
        }
        hdrs.removeAll();
        
        while (game_file.good() && !game_file.eof())
        {
           if ((c = game_file.get()) == '[')
           {
               game_file.putback(c);
               break;
           }
        }
    } 
    return 1;
}


int ChessIO::get_header(const ArasanVector<Header> &hdrs, 
  const string &key, string &val)
{
    val = "";
    for (int i=0; i <hdrs.length(); i++)
    {
         const Header &p = hdrs[i];
         if (p.tag() == key)
         {
             val = p.value();
             return 1;
         }
    }
    return 0;
}

void ChessIO::add_header(ArasanVector <Header> &hdrs,
  const string &key, const string & val)
{
   hdrs.append(Header(key,val));
}

int ChessIO::load_fen(istream &ifs, Board &board)
{                  
    ifs >> board;
    board.state.moveCount = 0;
    return ifs.good();
}

int ChessIO::store_fen( ostream &ofile, const Board &board)
{
    ofile << board << endl;
    return ofile.good();
}

int ChessIO::store_pgn(ostream &ofile,MoveArray &moves, 
                        const ColorType computer_side,
                        const string &result,
                        ArasanVector<Header> &headers)
{
    // Write standard PGN header.

    int i;
    string gameResult = result;
    if (result.length() == 0)
       gameResult = "*";
    string val;
    ArasanVector <Header> newHeaders;
    if (!get_header(headers, "Event", val))
       add_header(newHeaders,"Event","?");
    else
       add_header(newHeaders,"Event",val);
    if (!get_header(headers, "Site", val))
       add_header(newHeaders,"Site","?");
    else
       add_header(newHeaders,"Site",val);
    if (!get_header(headers, "Date", val))
    {
       char dateStr[15];
       time_t tm = time(NULL);
       struct tm *t = localtime(&tm);
       sprintf(dateStr,"%4d.%02d.%02d",t->tm_year+1900,t->tm_mon+1,
                        t->tm_mday);
       add_header(newHeaders,"Date",dateStr);
    }
    else
       add_header(newHeaders,"Date",val);
    if (!get_header(headers, "Round", val))
       add_header(newHeaders,"Round","?");
    else
       add_header(newHeaders,"Round",val);
 
    string name("Arasan ");
    name += Arasan_Version;
    if (computer_side == White)
    {
       if (!get_header(headers, "White",val))
       {
         add_header(newHeaders, "White", name);
       }
       else
         add_header(newHeaders,"White",val);
       if (!get_header(headers, "Black",val))
         add_header(newHeaders,"Black","?");
       else
         add_header(newHeaders,"Black",val);
    } 
    else
    {
       if (!get_header(headers, "White",val))
         add_header(newHeaders,"White","?");
       else
         add_header(newHeaders,"White",val);
       if (!get_header( headers, "Black",val))
         add_header(newHeaders, "Black", name);
       else
         add_header(newHeaders,"Black",val);
    }
    // "Result" may contain a comment. Don't put this in the
    // PGN header.
    string shortResult = gameResult;
    string longResult = gameResult;
    string::size_type space = gameResult.find(" ");
    if (space >0 && space < MAX_PATH)
       shortResult = gameResult.erase(space);
    if (!get_header(headers,"Result",val))
       add_header(newHeaders,"Result",shortResult);
    else
       add_header(newHeaders,"Result",val);

    // We have now written all the mandatory headers.
    // Add any more headers that were passed into us.

    int n = headers.length();
    for (i=0;i<n;i++)
    {
        Header p = headers[i];
        if (p.tag() !="Event" &&
            p.tag() !="Site" &&
            p.tag() != "Date" &&
            p.tag() != "Round" &&
            p.tag() != "White" && 
            p.tag() != "Black" &&
            p.tag() != "Result")
            add_header(newHeaders,p.tag(),p.value());
    }

    // write headers and cleanup
    return store_pgn(ofile,moves,longResult,newHeaders);
}

int ChessIO::store_pgn(ostream &ofile, MoveArray &moves,const string &result,
                         ArasanVector<Header> &headers)
{
    int i,n;
    n = headers.length();
    for (i =0; i<n; i++) {
        Header p = headers[i];
        ofile << "[" << p.tag() << " \"" << p.value() << "\"]" << endl;
        
    }
    ofile << endl;

    // Write game moves.
    int len = moves.length();
    stringstream buf;
    for (int i = 0; i < len; i++) {
        const MoveRecord &e = moves[i];
        stringstream numbuf;
        if (i % 2 == 0) {
            numbuf << (i/2)+1 << ". ";
        }
        const int image_size = (int)e.image().length();
        if ((int)buf.tellp() + image_size + numbuf.str().length() + 1 >= PGN_MARGIN) {
            ofile << buf.str() << endl;
            buf.str("");
        }
        if (buf.tellp() != (streampos)0) {
           buf << ' ';
        }
        buf << numbuf.str() << e.image();
    }
    if ((int)buf.tellp() + result.length() + 1 >= PGN_MARGIN) {
        ofile << buf.str() << endl;
        buf.str("");
        buf << result;
    } else {
        buf << ' ' << result;
    }
    ofile << buf.str() << endl;
    if (!ofile) {
        cerr << "warning: error saving game" << endl;
        return 0;
    }
    return 1;
}

int ChessIO::readEPDRecord(istream &ifs, Board &board, EPDRecord &rec)
{
    rec.clear();
    // read FEN description
    ifs >> board;
    if (ifs.eof())
       return 0;
    if (!ifs.good())
    {
        rec.setError("Bad EPD record: FEN board description missing or invalid");
        ifs.ignore(255,'\n');
        return 1;
    }
    // read EPD commands
    int c = 0;
    while (ifs.good() && (c = ifs.get()) != EOF)
    {
        int saw_eoln = 0;
        while (isspace(c))
        {
            if (c == '\n' || c == '\r')
               saw_eoln++;
            c = ifs.get();
        }
        if (c == 0) break;
        if (saw_eoln)
        {
            ifs.putback(c);
            break;
        }
        // collect command
        char cmd[20], val[256];
        char *p = cmd;
        int count = 0;
        while (ifs.good() && !isspace(c) && count < 19)
        {
            *p++ = c;
            c = ifs.get();
            ++count;
        }
        *p = '\0';
        while (isspace(c))
        {
            c = ifs.get();
        }
        p = val;
        int quoted = (c == '"');
        count = 0;
        while (ifs.good() && 
               count < 255 &&
               (c != ';' || quoted))
        {
            *p++ = c;
            c = ifs.get();
            if (quoted && c == '"')
            {
               *p++ = c;
               quoted = 0;
               break;
            }
        }
        if (quoted)
        {
            rec.setError("Missing end quote in EPD record");
            ifs.ignore(255,'\n');
            return 1;
        }
        *p = '\0';
        if (*cmd && *val)
            rec.add(cmd,val);
        while (!ifs.eof() && c != ';') c = ifs.get();
    }
    if (c == '\n') c = ifs.get();
    return 1;
}

void ChessIO::collect_headers(istream &game_file,ArasanVector <Header>&hdrs, long &first)
{
        first = -1L;
        int c;
        bool firstTag = true;
        while (!game_file.eof())
        {
            char tag[MAX_TAG+1];
            char val[MAX_VAL+1];
            c = skip_space(game_file);
            if (c!='[')
            {
                game_file.putback(c);
                break;
            }
            else
            {
                if (first == -1)
                    first = (long)game_file.tellg()-1;
                int t = 0;
                c = skip_space(game_file);
                while (!game_file.eof())
                {
                    if (!isspace(c) && c != '[' &&
                        c != '"' && t < MAX_TAG)
                        tag[t++] = c;
                    else
                        break;
                    c = game_file.get();
                } 
                tag[t] = '\0';
                if (firstTag)
                {
                    if (strcmp(tag,"vent") == 0)
                    {
                       // It appears that there is a bug in iostream.
                       // Calling tellg (above) sometimes causes
                       // a missing char when the next read occurs.
                       // So we get a partial tag. This is a workaround.
                       strcpy(tag,"Event");
                    }
                    else if (strcmp(tag,"Event") != 0)
                    {
                       if (c == '[') game_file.putback(c);
                       continue;
                    }
                }
                firstTag = false;
                if (isspace(c))
                    c = skip_space(game_file);
                if (c=='"')
                {
                    int v = 0;
                    while (!game_file.eof())
                    {
                        c = game_file.get();
                        if (c != '"' && v < MAX_VAL)
                            val[v++] = c;
                        else
                            break;
                    }
                    val[v] = '\0';
                }
                //else
                //    cerr << "bad tag" << endl;
                hdrs.append(Header(tag,val));
                while (!game_file.eof() && c != ']')
                    c = game_file.get();
            }
        }
}

ChessIO::Token ChessIO::get_next_token(istream &game_file, char *buf, int limit)
{
    Token tok = Unknown;
    int count = 0;
    int c = EOF;
    while (!game_file.eof()) {
      c = game_file.get();
      if (!isspace(c)) break;
    }
    if (c == EOF)
       return Eof;
    else if (c=='{')
    {
        *buf++ = c;
        ++count;
        while (game_file.good() && c != '}')
        {
            c = game_file.get();
            if (count < limit-1)
            {
               *buf++ = c;
               ++count;
            }
        }
        if (c=='}' && count < limit-1)
        {
            *buf++ = c;
        }
        *buf = '\0';
        return Comment;
        //c = game_file.get();
    }
    if (isdigit(c))
    {
        int nextc = game_file.get();
        if (c == '0')
        {
           // peek at next char.
           if (nextc == '-')
           {
               // some so-called PGN files have 0-0 or 0-0-0 for
               // castling.  To handle this, we need to peek ahead
               // one more character.
               int nextc2 = game_file.peek();
               if (toupper(nextc2) == 'O' || nextc2 == '0')
               {            
                  // castling, we presume
                  buf[count++] = c;
                  c = nextc;
                  while (game_file.good() && (c == '-' ||
                    c == '0' || toupper(c) == 'O' || c == '+'))
                  {
                     if (count < limit-1)
                        buf[count++] = c;
                     c = game_file.get();
                  }
                  buf[count] = '\0';
                  game_file.putback(c);
                  return GameMove;
               }
           }
        }
        if (nextc == '-' || nextc == '/') // assume result
        {
            buf[count++] = c;
            c = nextc;
            while (!game_file.eof() && game_file.good() && !isspace(c))
            {
                if (count < limit-1)
                    buf[count++] = c;
                c = game_file.get();
            }
            tok = Result;
        }
        else
        {
            // Assume we have a move number.
            buf[count++] = c;
            c = nextc;
            while (game_file.good() && (isdigit(c) || c == '.'))
            {
               if (count < limit-1)
                  buf[count++] = c;
               c = game_file.get();
            }
            game_file.putback(c);
            return Number;
       }
       buf[count] = '\0';
   }           
   else if (isalpha(c))
   {
       while (game_file.good() && (isalnum(c) 
              || c == '-' || c == '=' || (c == '+')))
       {
           if (count < limit-1)
               buf[count++] = c;
           c = game_file.get();
       }
       buf[count] = '\0';
       game_file.putback(c);
       return GameMove;
   }
   else if (c == '#') // "Checkmate"
   {
       *buf = c; *(buf+1) = '\0';
       tok = Ignore;
   }
   else if (c == '*')
   {
       *buf = c; *(buf+1) = '\0';
       tok = Result;
   }
   else
   {
       *buf = c; *(buf+1) = '\0';
       tok = Unknown;
   }
   return tok;
} 


