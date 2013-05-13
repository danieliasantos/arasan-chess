// Copyright 1994-2012 by Jon Dart. All Rights Reserved.
//
#include "movegen.h"
#include "attacks.h"
#include "util.h"
#include "debug.h"
#include "searchc.h"
#include "search.h"
#include "legal.h"
#include "history.h"
#include <iostream>
#include <fstream>
#include <algorithm>
using namespace std;

extern const int Direction[2];

static FORCEINLINE void swap( Move moves[], int i, int j)
{
   Move tmp = moves[j];
   moves[j] = moves[i];
   moves[i] = tmp;
}


static FORCEINLINE void swap( Move moves[], int scores[], int i, int j)
{
   Move tmp = moves[j];
   moves[j] = moves[i];
   moves[i] = tmp;
   int scoreTmp = scores[j];
   scores[j] = scores[i];
   scores[i] = scoreTmp;
}

void MoveGenerator::sortMoves(Move moves[], int scores[], int n) {
    if (n == 2) {
        if (scores[1] > scores[0]) {
            swap(moves,0,1);
        }
    } else if (n>1) {
       // insertion sort
       for (int i = 1; i < n; i++) {
          int key = scores[i];
          Move m = moves[i];
          int j = i-1;
          for (; j >= 0 && scores[j] < key; j--) {
              moves[j+1] = moves[j];
              scores[j+1] = scores[j];
          }
          moves[j+1] = m;
          scores[j+1] = key;
       }
    }
}


RootMoveGenerator::RootMoveGenerator(const Board &board,
SearchContext *s,
Move pvMove,
int trace)
: MoveGenerator(board,s,0,pvMove,trace)
{
   batch = moves;
   batch_count = MoveGenerator::generateAllMoves(batch,0);
#ifdef _TRACE
   cout << "root moves:" << endl;
#endif
   for (int i=0; i < batch_count; i++) {
#ifdef _TRACE
         MoveImage(batch[i],cout); cout << endl;
#endif
     MoveEntry me;
     me.move = batch[i];
     me.nodes = 0ULL;
     me.score = 0;
     moveList.push_back(me);
   }
   if (board.checkStatus()==InCheck) {
     if (batch_count == 1) {
        SetForced(moveList[0].move);
     }
   }
   else {
     // exclude illegal moves
     const BoardState state = board.state;
     Board tmp(board);
     int j = 0;
     for (int i=0; i<batch_count; i++) {
        Move &move = moveList[i].move;
        int score = (Capture(move) != Empty) ? see(board,move) : 0;
        tmp.doMove(move);
        if (tmp.anyAttacks(tmp.kingSquare(tmp.oppositeSide()),tmp.sideToMove())) {
          // move was illegal, own king is in check
           tmp.undoMove(move,state);
           continue;
        }
        if (j != i) {
            // compress the move list so illegal moves are excluded
            moveList[j] = moveList[i];
        }
        moveList[j++].score = score;
        tmp.undoMove(move,state);
     }
     for (int i = j; i < batch_count; i++) {
         moveList.pop_back();
     }
     reorderByScore();
     batch_count = j;
   }
   phase = LAST_PHASE;
}


int RootMoveGenerator::generateAllMoves(NodeInfo *, SplitPoint *)
{
   // There's nothing to be done here since we generated moves in the
   // constructor. Just return the number of moves remaining.
   return batch_count-index;
}

static bool compareNodes(const MoveEntry &a, const MoveEntry &b) {
  return (a.nodes > b.nodes) ;
}

static bool compareScores(const MoveEntry &a, const MoveEntry &b) {
  return a.score > b.score;
}

void RootMoveGenerator::reorder(Move pvMove,int depth)
{
   index = 0;  // reset so we will fetch moves again
   phase = START_PHASE;
   int i; int j = 0;
   for (i = 0; i < batch_count; i++) {
      ClearUsed(moveList[i].move);
      if (MovesEqual(moveList[i].move,pvMove)) {
         // move PV move to front
         MoveEntry tmp = moveList[i];
         moveList[i] = moveList[0];
         moveList[0] = tmp;
         SetPhase(moveList[0].move,HASH_MOVE_PHASE);
         ++j;
      } else if (CaptureOrPromotion(moveList[i].move) &&
                 see(board,moveList[i].move) >= 0) {
          SetPhase(moveList[i].move,WINNING_CAPTURE_PHASE);
      } else {
          SetPhase(moveList[i].move,HISTORY_PHASE);
      }
   }
   if (depth>=4 && batch_count > 1) {
     // re-order based on the cumulative node counts
     std::sort(moveList.begin()+j,moveList.end(),compareNodes);
   }
}


void RootMoveGenerator::exclude(Move *exclude, int num_exclude)
{
  for (int i = 0; i < batch_count; i++) {
      ClearUsed(moveList[i].move);
      for (int j = 0; j < num_exclude; j++) {
         if (MovesEqual(moveList[i].move,exclude[j])) {
            SetUsed(moveList[i].move);
         }
      }
   }
}


void RootMoveGenerator::exclude(Move exclude) {
   for (int i = 0; i < batch_count; i++) {
      if (MovesEqual(moveList[i].move,exclude)) {
         SetUsed(moveList[i].move);
         break;
      }
   }
}


void RootMoveGenerator::reorderByScore() {
   index = 0;  // reset so we will fetch moves again
   phase = START_PHASE;
   if (moveList.size() <= 1)
      return;
   for (unsigned i = 0; i < moveList.size(); i++) {
      ClearUsed(moveList[i].move);
   }
   std::sort(moveList.begin(),moveList.end(),compareScores);
}

int MoveGenerator::initialSortCaptures (Move *moves,int captures) {
   if (captures > 1) {
      int scores[40];
      ASSERT(captures < 40);
      for (int i = index; i < index+captures; i++) {
          scores[i] = MVV_LVA(moves[i]);
      }
      sortMoves(moves,scores,captures);
   }
   return captures;
}


Move MoveGenerator::nextEvasion() {
   if (batch_count==0) {
     batch_count = generateEvasions(moves);
     if (batch_count == 0) {
        return NullMove;
     }
     else if (batch_count > 1) {
       int scores[40];
       ASSERT(batch_count < 40);
       const int flag = (batch_count == 2);
       int poscaps = 0, negcaps = 0;
       for (int i = 0; i < batch_count; i++) {
          if (CaptureOrPromotion(moves[i])) {
             scores[i] = see(board,moves[i]);
             if (scores[i] > 0) {
                ++poscaps;
             } else if (scores[i] < 0) {
                ++negcaps;
             }
          } else {
             scores[i] = 0;
          }
          if (flag) SetForced2(moves[i]);
       }
       if (poscaps > 1) {
          sortMoves(moves,scores,negcaps ? batch_count : poscaps);
       } else if (negcaps) {
          sortMoves(moves,scores,batch_count);
       }
     }
     else {
       SetForced(moves[0]);
     }
   }
   if (index < batch_count) {
      return moves[index++];
   }
   else
      return NullMove;
}


int MoveGenerator::getBatch(Move *&batch,int &index)
{
   int numMoves  = 0;
   batch = moves;
   while(numMoves == 0 && phase<LOSERS_PHASE) {
      phase++;
#ifdef _TRACE
      if (master) {
         indent(ply); cout << "phase = " << phase << endl;
      }
#endif
      switch(phase) {
         case HASH_MOVE_PHASE:
         {
            if (!IsNull(hashMove) && validMove(board,hashMove)) {
               *moves = hashMove;
               SetPhase(*moves,phase);
#ifdef _TRACE
               if (master) {
                  indent(ply); cout << "hash move = ";
                  MoveImage(hashMove,cout);
                  cout << endl;
               }
#endif
               index = 0;
               numMoves = 1;
            }
            break;
         }
         case WINNING_CAPTURE_PHASE:
         {
            int captures = MoveGenerator::generateCaptures(moves);
            index = 0;
            numMoves = initialSortCaptures(moves,captures);
            break;
         }
         case KILLER1_PHASE:
         {
            if (!context) continue;
            context->getKillers(ply,killer1,killer2);
            if (ply >=2)
               context->getKillers(ply-2,killer3,killer4);
            else
               killer3 = killer4 = NullMove;
            if (!IsNull(killer1) && !MovesEqual(hashMove,killer1)) {
               if (validMove(board,killer1)) {
                  SetPhase(killer1,KILLER1_PHASE);
                  moves[numMoves++] = killer1;
                  index = 0;
               }
            }
            if (ply>=2 && !IsNull(killer3) && !MovesEqual(hashMove,killer3) &&
            !MovesEqual(killer1,killer3)) {
               if (validMove(board,killer3)) {
                  SetPhase(killer3,KILLER1_PHASE);
                  moves[numMoves++] = killer3;
                  index = 0;
               }
            }
         }
         break;
         case KILLER2_PHASE:
         {
            if (!context) continue;
            if (!IsNull(killer2) && !MovesEqual(hashMove,killer2) &&
            (ply<2||!MovesEqual(killer2,killer3))) {
               if (validMove(board,killer2)) {
                  SetPhase(killer2,KILLER1_PHASE);
                  moves[numMoves++] = killer2;
                  index = 0;
               }
            }
            if (ply>=2 && !IsNull(killer4) &&
               !MovesEqual(hashMove,killer4) &&
               !MovesEqual(killer1,killer4) &&
               !MovesEqual(killer2,killer4)) {
               if (validMove(board,killer4)) {
                  SetPhase(killer4,KILLER1_PHASE);
                  moves[numMoves++] = killer4;
                  index = 0;
               }
            }
         }
         break;
         case HISTORY_PHASE:
         {
            numMoves = generateNonCaptures(moves);
            if (numMoves) {
               int scores[Constants::MaxMoves];
               for (int i = 0; i < numMoves; i++) {
                  if (MovesEqual(hashMove,moves[i])) {
                     SetUsed(moves[i]);
                     scores[i] = 0;
                     continue;
                  }
                  else if (MovesEqual(killer1,moves[i]) ||
                  MovesEqual(killer2,moves[i])) {
                     SetUsed(moves[i]);
                     scores[i] = 0;
                     continue;
                  }
                  else if ((MovesEqual(killer3,moves[i]) ||
                  MovesEqual(killer4,moves[i]))) {
                     SetUsed(moves[i]);
                     scores[i] = 0;
                     continue;
                  }
                  SetPhase(moves[i],HISTORY_PHASE);
                  if (context) {
                      scores[i] = History::scoreForOrdering(moves[i],board.sideToMove());
                  }
               }
               if (numMoves > 1) {
                  sortMoves(moves,scores,numMoves);
               }
            }
            index = 0;
            break;
         }
         case LOSERS_PHASE:
         {
            if (losers_count) {
               batch = losers;
            }
            numMoves = losers_count;
            index = 0;
            break;
         }
         default:
            break;
      }                                           // end switch
   }                                              // end for
#ifdef DEBUG
   for (int = 0; i < numMoves; i++)
      if (Capture(batch[i])==King) ASSERT(0);
#endif
   return numMoves;
}


int MoveGenerator::generateNonCaptures(Move *moves)
{
   int numMoves = 0;
   // castling moves
   const ColorType side = board.sideToMove();
   CastleType CS = board.castleStatus(side);
   if ((CS == CanCastleEitherSide) ||
   (CS == CanCastleKSide)) {
      const Square kp = board.kingSquare(side);
#ifdef _DEBUG
      if (side == White) {
         ASSERT(kp == E1);
         ASSERT(board[kp+3] == WhiteRook);
      }
      else {
         ASSERT(kp == E8);
         ASSERT(board[kp+3] == BlackRook);
      }
#endif
      if (board[kp + 1] == EmptyPiece &&
         board[kp + 2] == EmptyPiece &&
         board.checkStatus() == NotInCheck &&
         !board.anyAttacks(kp + 1,OppositeColor(side)) &&
         !board.anyAttacks(kp + 2,OppositeColor(side)))
         // can castle
         moves[numMoves++] = CreateMove(kp, kp+2, King, Empty,
            Empty, KCastle);
   }
   if ((CS == CanCastleEitherSide) ||
   (CS == CanCastleQSide)) {
      const Square kp = board.kingSquare(side);
      if (board[kp - 1] == EmptyPiece &&
         board[kp - 2] == EmptyPiece &&
         board[kp - 3] == EmptyPiece &&
         board.checkStatus() == NotInCheck  &&
         !board.anyAttacks(kp - 1,OppositeColor(side)) &&
         !board.anyAttacks(kp - 2,OppositeColor(side)))
         // can castle
         moves[numMoves++] = CreateMove(kp, kp-2, King, Empty,
            Empty, QCastle);
   }
   // non-pawn moves:
   Square start,dest;
   Bitboard dests;
   Bitboard knights(board.knight_bits[side]);
   while (knights.iterate(start)) {
      dests = Attacks::knight_attacks[start] & ~board.allOccupied;
      while (dests.iterate(dest)) {
         moves[numMoves++] =
             CreateMove(start,dest,Knight);
      }
   }
   start = board.kingSquare(side);
   dests = Attacks::king_attacks[start] & ~board.allOccupied &
               ~Attacks::king_attacks[board.kingSquare(board.oppositeSide())];
   while (dests.iterate(dest)) {
      moves[numMoves++] =
        CreateMove(start,dest,King);
   }
   Bitboard bishops(board.bishop_bits[side] | board.queen_bits[side]);
   while (bishops.iterate(start)) {
      Bitboard dests(board.bishopAttacks(start) & ~board.allOccupied);
      while (dests.iterate(dest)) {
         moves[numMoves++] =
            CreateMove(start,dest,TypeOfPiece(board[start]));
      }
   }
   Bitboard rooks(board.rook_bits[side] | board.queen_bits[side]);
   while (rooks.iterate(start)) {
      Bitboard dests(board.rookAttacks(start) & ~board.allOccupied);
      while (dests.iterate(dest)) {
          moves[numMoves++] =
            CreateMove(start,dest,TypeOfPiece(board[start]));
      }
   }
   // pawn moves
   if (board.sideToMove() == White) {
      Bitboard pawns(board.pawn_bits[White]);
      pawns.shl8();
        // exclude promotions
      pawns &= ~(board.allOccupied | Attacks::rank_mask[7]);
      Square sq;
      while (pawns.iterate(sq)) {
         moves[numMoves++] = CreateMove(sq-8,sq,Pawn);
         if (Rank<White>(sq)==3 && board[sq+8] == EmptyPiece)
            moves[numMoves++] = CreateMove(sq-8,sq+8,Pawn);
      }
   }
   else {
      Bitboard pawns(board.pawn_bits[Black]);
      pawns.shr8();
        // exclude promotions
      pawns &= ~(board.allOccupied | Attacks::rank_mask[0]);     
      Square sq;
      while (pawns.iterate(sq)) {
         moves[numMoves++] = CreateMove(sq+8,sq,Pawn);
         if (Rank<Black>(sq)==3 && board[sq-8] == EmptyPiece)
            moves[numMoves++] = CreateMove(sq+8,sq-8,Pawn);
      }
   }
   return numMoves;
}


int MoveGenerator::generateCaptures(Move * moves, const Bitboard &targets)
{
   int numMoves = 0;

   if (board.sideToMove() == White) {
      Bitboard pawns(board.pawn_bits[White]);
      Bitboard pawns1 = pawns;
      pawns1.shl(7);
      pawns1 &= ~0x8080808080808080ULL;
      pawns1 &= board.occupied[Black];
      Square dest;
      while (pawns1.iterate(dest)) {
         Square start = dest-7;
         if (Rank<White>(start) == 7) {
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Queen,Promotion);
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Knight,Promotion);
            if (ply == 0) {
               moves[numMoves++] =
                  CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Rook,Promotion);
               moves[numMoves++] =
                  CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Bishop,Promotion);
            }
         }
         else if (targets.isSet(dest)) {
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]));
         }
      }
      pawns1 = pawns;
      pawns1.shl(9);
      pawns1 &= ~0x0101010101010101ULL;
      pawns1 &= board.occupied[Black];
      while (pawns1.iterate(dest)) {
         Square start = dest-9;
         if (Rank<White>(start) == 7) {
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Queen,Promotion);
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Knight,Promotion);
            if (ply == 0) {
               moves[numMoves++] =
                  CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Rook,Promotion);
               moves[numMoves++] =
                  CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Bishop,Promotion);
            }
         }
         else if (targets.isSet(dest)) {
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]));
         }
      }
      pawns1 = pawns;
      pawns1 &= Attacks::rank_mask[6];
      pawns1.shl8();
      pawns1 &= ~board.allOccupied;
      while (pawns1.iterate(dest)) {
         Square start = dest - 8;
         moves[numMoves++] =
            CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Queen,Promotion);
         moves[numMoves++] =
            CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Knight,Promotion);
         if (ply == 0) {
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Rook,Promotion);
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Bishop,Promotion);
         }
      }
      Square epsq = board.enPassantSq();
      if (!IsInvalid(epsq) && targets.isSet(epsq)) {
         ASSERT(TypeOfPiece(board[epsq])==Pawn);
         if (File(epsq) != 8 && board[epsq + 1] == WhitePawn) {
            dest = epsq + 8;
            if (board[dest] == EmptyPiece)
               moves[numMoves++] =
                  CreateMove(epsq+1,dest,Pawn,Pawn,Empty,
                  EnPassant);
         }
         if (File(epsq) != 1 && board[epsq - 1] == WhitePawn) {
            dest = epsq + 8;
            if (board[dest] == EmptyPiece)
               moves[numMoves++] =
                  CreateMove(epsq-1,dest,Pawn,Pawn,Empty,
                  EnPassant);
         }
      }
   }
   else {
      Bitboard pawns(board.pawn_bits[Black]);
      Bitboard pawns1 = pawns;
      pawns1.shr(7);
      pawns1 &= ~0x0101010101010101ULL;
      pawns1 &= board.occupied[White];
      Square dest;
      while (pawns1.iterate(dest)) {
         Square start = dest+7;
         if (Rank<Black>(start) == 7) {
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Queen,Promotion);
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Knight,Promotion);
            if (ply == 0) {
               moves[numMoves++] =
                  CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Rook,Promotion);
               moves[numMoves++] =
                  CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Bishop,Promotion);
            }
         }
         else if (targets.isSet(dest)) {
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]));
         }
      }
      pawns1 = pawns;
      pawns1.shr(9);
      pawns1 &= ~0x8080808080808080ULL;
      pawns1 &= board.occupied[White];
      while (pawns1.iterate(dest)) {
         Square start = dest+9;
         if (Rank<Black>(start) == 7) {
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Queen,Promotion);
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Knight,Promotion);
            if (ply == 0) {
               moves[numMoves++] =
                  CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Rook,Promotion);
               moves[numMoves++] =
                  CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Bishop,Promotion);
            }
         }
         else if (targets.isSet(dest)) {
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]));
         }
      }
      pawns1 = pawns;
      pawns1 &= Attacks::rank_mask[1];
      pawns1.shr8();
      pawns1 &= ~board.allOccupied;
      while (pawns1.iterate(dest)) {
         Square start = dest + 8;
         moves[numMoves++] =
            CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Queen,Promotion);
         moves[numMoves++] =
            CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Knight,Promotion);
         if (ply == 0) {
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Rook,Promotion);
            moves[numMoves++] =
               CreateMove(start,dest,Pawn,TypeOfPiece(board[dest]),Bishop,Promotion);
         }
      }
      Square epsq = board.enPassantSq();
      if (!IsInvalid(epsq) && targets.isSet(epsq)) {
         ASSERT(TypeOfPiece(board[epsq])==Pawn);
         if (File(epsq) != 8 && board[epsq + 1] == BlackPawn) {
            dest = epsq - 8;
            if (board[dest] == EmptyPiece)
               moves[numMoves++] =
                  CreateMove(epsq+1,dest,Pawn,Pawn,Empty,
                  EnPassant);
         }
         if (File(epsq) != 1 && board[epsq - 1] == BlackPawn) {
            dest = epsq - 8;
            if (board[dest] == EmptyPiece)
               moves[numMoves++] =
                  CreateMove(epsq-1,dest,Pawn,Pawn,Empty,
                  EnPassant);
         }
      }
   }
   const ColorType side = board.sideToMove();
   Bitboard knights(board.knight_bits[side]);
   Square start, dest;
   while (knights.iterate(start)) {
      Bitboard dests(Attacks::knight_attacks[start] & targets);
      while (dests.iterate(dest)) {
         moves[numMoves++] =
            CreateMove(start,dest,Knight,TypeOfPiece(board[dest]));
     }
   }
   Bitboard bishops(board.bishop_bits[side] | board.queen_bits[side]);
   while (bishops.iterate(start)) {
      Bitboard dests(board.bishopAttacks(start) & targets);
      Square dest;
      while (dests.iterate(dest)) {
         moves[numMoves++] =
           CreateMove(start,dest,TypeOfPiece(board[start]),TypeOfPiece(board[dest]));
      }
   }
   Bitboard rooks(board.rook_bits[side] | board.queen_bits[side]);
   while (rooks.iterate(start)) {
      Bitboard dests(board.rookAttacks(start) & targets);
      Square dest;
      while (dests.iterate(dest)) {
          moves[numMoves++] =
             CreateMove(start,dest,
                        TypeOfPiece(board[start]),TypeOfPiece(board[dest]));
      }
   }
   start = board.kingSquare(side);
   Bitboard dests(Attacks::king_attacks[start] & targets);
   while (dests.iterate(dest)) {
      moves[numMoves++] =
         CreateMove(start,dest,King,TypeOfPiece(board[dest]));
   }
   return numMoves;
}


MoveGenerator::MoveGenerator( const Board &ABoard,
SearchContext *s,
unsigned curr_ply, Move pvMove,
int trace)
:
board(ABoard),
context(s),
ply(curr_ply),
moves_generated(0),
losers_count(0),
index(0),
batch_count(0),
forced(0),
phase(START_PHASE),
hashMove(pvMove),
master(trace)
{
}


int MoveGenerator::generateEvasions(Move * moves)
{
   int n = generateEvasionsCaptures(moves);
   n += generateEvasionsNonCaptures(moves+n);
   return n;
}


int MoveGenerator::generateEvasionsNonCaptures(Move * moves)
{
   int num_moves = 0;
   const Square kp = board.kingSquare(board.sideToMove());
   if (num_attacks == 1) {
      // try to interpose a piece
      if (Sliding(board[source])) {
         Bitboard btwn_squares;
         board.between(source,kp,btwn_squares);
         if (!btwn_squares.is_clear()) {
            // blocking pawn moves
            if (board.sideToMove() == White) {
               Bitboard pawns(board.pawn_bits[White]);
               pawns.shl8();
               pawns &= ~board.allOccupied;
               Bitboard pawns1(pawns);
               pawns &= btwn_squares;
               Square sq;
               while (pawns.iterate(sq)) {
                  if (!board.isPinned(board.sideToMove(), WhitePawn, sq-8, sq)) {
                     if (Rank<White>(sq) == 8) {
                        // interposition is a promotion
                        moves[num_moves++] = CreateMove(
                           sq-8, sq, Pawn, Empty, Queen, Promotion);
                        moves[num_moves++] = CreateMove(
                           sq-8, sq, Pawn, Empty, Rook, Promotion);
                        moves[num_moves++] = CreateMove(
                           sq-8, sq, Pawn, Empty, Knight, Promotion);
                        moves[num_moves++] = CreateMove(
                           sq-8, sq, Pawn, Empty, Bishop, Promotion);
                     }
                     else {
                        moves[num_moves++] = CreateMove(sq-8, sq, Pawn, Empty);
                     }
                  }
               }
               pawns1 &= Attacks::rank_mask[2];
               if (!pawns1.is_clear()) {
                  pawns1.shl8();
                  pawns1 &= ~board.allOccupied;
                  pawns1 &= btwn_squares;
                  while (pawns1.iterate(sq)) {
                     if (!board.isPinned(White, WhitePawn, sq-16, sq))
                        moves[num_moves++] = CreateMove(sq-16,sq,Pawn,Empty);
                  }
               }
            }
            else {
               Bitboard pawns(board.pawn_bits[Black]);
               pawns.shr8();
               pawns &= ~board.allOccupied;
               Bitboard pawns1(pawns);
               pawns &= btwn_squares;
               Square sq;
               while (pawns.iterate(sq)) {
                  if (!board.isPinned(Black, BlackPawn, sq+8, sq)) {
                     if (Rank<Black>(sq) == 8) {
                        // interposition is a promotion
                        moves[num_moves++] = CreateMove(
                           sq+8, sq, Pawn, Empty, Queen, Promotion);
                        moves[num_moves++] = CreateMove(
                           sq+8, sq, Pawn, Empty, Rook, Promotion);
                        moves[num_moves++] = CreateMove(
                           sq+8, sq, Pawn, Empty, Knight, Promotion);
                        moves[num_moves++] = CreateMove(
                           sq+8, sq, Pawn, Empty, Bishop, Promotion);
                     }
                     else {
                        moves[num_moves++] = CreateMove(sq+8, sq, Pawn, Empty);
                     }
                  }
               }
               pawns1 &= Attacks::rank_mask[5];
               if (!pawns1.is_clear()) {
                  pawns1.shr8();
                  pawns1 &= ~board.allOccupied;
                  pawns1 &= btwn_squares;
                  while (pawns1.iterate(sq)) {
                     if (!board.isPinned(board.sideToMove(), BlackPawn, sq+16, sq))
                        moves[num_moves++] = CreateMove(sq+16,sq,Pawn,Empty);
                  }
               }
            }
            // other blocking pieces
            Bitboard pieces(board.occupied[board.sideToMove()]);
            pieces &= ~board.pawn_bits[board.sideToMove()];
            Square loc;
            while (pieces.iterate(loc)) {
               switch (TypeOfPiece(board[loc])) {
                  case EmptyPiece:
                  case Pawn:
                     break;
                  case Knight:
                  {
                    Bitboard dests(Attacks::knight_attacks[loc] & btwn_squares);
                     Square sq;
                     while (dests.iterate(sq)) {
                        if (!board.isPinned(board.sideToMove(), board[loc], loc, sq))
                           moves[num_moves++] =
                              CreateMove(loc,sq,Knight,TypeOfPiece(board[sq]));
                     }
                  }
                  break;
                  case Bishop:
                  {
                     Square dest;
                     Bitboard dests(board.bishopAttacks(loc) & btwn_squares);
                     while (dests.iterate(dest)) {
                        if (board.clear(loc,dest) &&
                           !board.isPinned(board.sideToMove(), board[loc], loc, dest))
                           moves[num_moves++] =
                              CreateMove(loc,dest,Bishop,TypeOfPiece(board[dest]));
                     }
                  }
                  break;
                  case Rook:
                  {
                     Square dest;
                     Bitboard dests(board.rookAttacks(loc) & btwn_squares);
                     while (dests.iterate(dest)) {
                        if (board.clear(loc,dest) && !board.isPinned(board.sideToMove(), board[loc], loc, dest)) {
                           moves[num_moves++] =
                              CreateMove(loc,dest,Rook,TypeOfPiece(board[dest]));
                        }
                     }
                  }
                  break;
                  case Queen:
                  {
                     Square dest;
                     Bitboard dests(board.rookAttacks(loc)|board.bishopAttacks(loc));
                     dests &= btwn_squares;
                     while (dests.iterate(dest)) {
                        if (board.clear(loc,dest) &&
                           !board.isPinned(board.sideToMove(), board[loc], loc, dest))
                           moves[num_moves++] =
                              CreateMove(loc,dest,Queen,TypeOfPiece(board[dest]));
                     }
                  }
                  break;
                  case King:
                     break;
               }
            }
         }
      }
   }
   // generate evasive moves that do not capture
   num_moves += generateEvasions(moves+num_moves,~board.allOccupied);
   return num_moves;
}


int MoveGenerator::generateEvasionsCaptures(Move * moves)
{
   int num_moves = 0;
   const Square kp = board.kingSquare(board.sideToMove());
   king_attacks = board.calcAttacks(kp, board.oppositeSide());
   if (king_attacks.is_clear()) {
     cout << board << endl;
         ASSERT(0);
   }
   num_attacks = king_attacks.bitCountOpt();
   if (num_attacks == 1) {
      // try to capture checking piece
      source = (Square)king_attacks.firstOne();

      ASSERT(source != InvalidSquare);
      Bitboard atcks(board.calcAttacks(source,board.sideToMove()));
      Square sq;
      const PieceType sourcePiece = TypeOfPiece(board[source]);
      while (atcks.iterate(sq)) {
         PieceType capturingPiece = TypeOfPiece(board[sq]);
         if (capturingPiece == King) {
            // We can capture with the king only if the piece
            // checking us is undefended.  But always allow a
            // capture *of* the king - for illegal move detection.
            if (TypeOfPiece(board[source]) == King ||
            !board.anyAttacks(source, board.oppositeSide())) {
               moves[num_moves++] = CreateMove(sq, source,
                  King, sourcePiece);
            }
         }
         else {
            if (!board.isPinned(board.sideToMove(), board[sq], sq, source)) {
               if (capturingPiece == Pawn &&
                   Rank(source,board.sideToMove()) == 8) {
                  moves[num_moves++] = CreateMove(
                     sq, source, Pawn, sourcePiece, Queen, Promotion);
                  moves[num_moves++] = CreateMove(
                     sq, source, Pawn, sourcePiece, Rook, Promotion);
                  moves[num_moves++] = CreateMove(
                     sq, source, Pawn, sourcePiece, Knight, Promotion);
                  moves[num_moves++] = CreateMove(
                     sq, source, Pawn, sourcePiece, Bishop, Promotion);
               }
               else {
                  moves[num_moves++] = CreateMove(sq, source, capturingPiece, sourcePiece);
               }
            }
         }
      }
      // Attacks::calcAttacks does not return en passant captures, so try
      // this as a special case
      if (board.enPassantSq() == source) {
         Square dest = source + 8 * Direction[board.sideToMove()];
         Piece myPawn = MakePiece(Pawn,board.sideToMove());
         if (File(source) != 8 && board[source + 1] == myPawn) {
            Piece tmp = board[source];
            Piece &place = (Piece &)board[source];
            place = EmptyPiece;                   // imagine me gone
            if (!board.isPinned(board.sideToMove(), board[source+1], source + 1, dest))
               moves[num_moves++] = CreateMove(source + 1, dest, Pawn,
                  Pawn, Empty, EnPassant);
            place = tmp;
         }
         if (File(source) != 1 && board[source - 1] == myPawn) {
            Piece tmp = board[source];
            Piece &place = (Piece &)board[source];
            place = EmptyPiece;                   // imagine me gone
            if (!board.isPinned(board.sideToMove(), board[source-1], source - 1, dest))
               moves[num_moves++] = CreateMove(source - 1, dest, Pawn, Pawn,
                  Empty, EnPassant);
            place = tmp;
         }
      }
   }
   // try evasions that capture pieces beside the attacker
   num_moves += generateEvasions(moves+num_moves,
      board.occupied[board.oppositeSide()]);

   return num_moves;
}


int MoveGenerator::generateEvasions(Move * moves,
const Bitboard &mask)
{
   int num_moves = 0;
   Square kp = board.kingSquare(board.sideToMove());
   Bitboard b(Attacks::king_attacks[kp]);
   b &= ~board.allPawnAttacks(board.oppositeSide());
   b &= ~Attacks::king_attacks[board.kingSquare(board.oppositeSide())];
   b &= mask;
   Square sq;
   while (b.iterate(sq)) {
      if (num_attacks > 1 || sq != source) {
         // We need to do some extra checking, since the board
         // info on attacks reflects the state before the move,
         // and we need to be sure that the destination square
         // is not attacked after the move.
         int illegal = 0;
         Bitboard it(king_attacks);
         Square atck_sq;
         while (it.iterate(atck_sq)) {
            Piece attacker = board[atck_sq];
            if (Sliding(attacker)) {
               int dir = Attacks::directions[atck_sq][sq];
               // check for movement in the direction of the
               // attacker:
               if (dir != 0 && (kp + dir == sq)) {
                  illegal = 1;
                  break;
               }
            }
         }
         if (!illegal && !board.anyAttacks(sq, board.oppositeSide())) {
            moves[num_moves++] = CreateMove(kp, sq, King, TypeOfPiece(board[sq]));
         }
      }
   }
   return num_moves;
}


int MoveGenerator::generateChecks(Move * moves)
{
   // Note: doesn't at present generate castling moves that check, or
   // discovered checks
   const Square kp = board.kingSquare(board.oppositeSide());
   Bitboard pieces(board.occupied[board.sideToMove()]);
   pieces &= ~board.pawn_bits[board.sideToMove()];
   Square loc;
   int numMoves = 0;
   while (pieces.iterate(loc)) {
      switch(TypeOfPiece(board[loc])) {
         case Knight:
         {
            Bitboard dests(Attacks::knight_attacks[loc]);
            dests &= ~board.allOccupied;
            dests &= Attacks::knight_attacks[kp];
            Square sq;
            while (dests.iterate(sq)) {
               moves[numMoves++] =
                  CreateMove(loc,sq,Knight);
            }
            break;
         }
         case King:
            break;
         case Bishop:
         {
            Bitboard dests(board.bishopAttacks(loc) & ~board.allOccupied);
            Bitboard dests2(board.bishopAttacks(kp) & ~board.allOccupied);
            dests &= dests2;
                    Square dest;
            while (dests.iterate(dest)) {
                moves[numMoves++] =
                   CreateMove(loc,dest,Bishop);
            }
            break;
         }
         case Rook:
         {
            Bitboard dests(board.rookAttacks(loc) & ~board.allOccupied);
            Bitboard dests2(board.rookAttacks(kp) & ~board.allOccupied);
            dests &= dests2;
            Square dest;
            while (dests.iterate(dest)) {
                moves[numMoves++] =
                   CreateMove(loc,dest,Rook);
            }
            break;
         }
         case Queen:
         {
            Bitboard dests(board.rookAttacks(loc) | board.bishopAttacks(loc));
            dests &= ~board.allOccupied;
            Bitboard dests2(board.bishopAttacks(kp) & ~board.allOccupied);
            Bitboard dests3(board.rookAttacks(kp) & ~board.allOccupied);
            dests &= (dests2 | dests3);
            Square dest;
            while (dests.iterate(dest)) {
                  moves[numMoves++] =
                     CreateMove(loc,dest,Queen);
            }
            break;
         }
         default:
           break;
      }
   }
   // pawn moves
   if (board.sideToMove() == White) {
      Square sq;
      Bitboard pawns(board.pawn_bits[White]);
      pawns.shl8();
      pawns &= ~(board.allOccupied | Attacks::rank_mask[7]);
      Bitboard pawns1(pawns & Attacks::pawn_attacks[kp][White]);
      while (pawns1.iterate(sq)) {
         moves[numMoves++] = CreateMove(sq-8,sq,Pawn);
      }
      pawns &= Attacks::rank_mask[2];
      pawns.shl8();
      pawns &= ~board.allOccupied;
      pawns &= Attacks::pawn_attacks[kp][White];
      while (pawns.iterate(sq)) {
         moves[numMoves++] = CreateMove(sq-16,sq,Pawn);
      }
   }
   else {
      Square sq;
      Bitboard pawns(board.pawn_bits[Black]);
      pawns.shr8();
      pawns &= ~(board.allOccupied | Attacks::rank_mask[0]);
      Bitboard pawns1(pawns & Attacks::pawn_attacks[kp][Black]);
      while (pawns1.iterate(sq)) {
         moves[numMoves++] = CreateMove(sq+8,sq,Pawn);
      }
      pawns &= Attacks::rank_mask[2];
      pawns.shl8();
      pawns &= ~board.allOccupied;
      pawns &= Attacks::pawn_attacks[kp][Black];
      while (pawns.iterate(sq)) {
         moves[numMoves++] = CreateMove(sq+16,sq,Pawn);
      }
   }
   return numMoves;
}


int MoveGenerator::generateAllMoves(NodeInfo *node, SplitPoint *split)
{
   // Force the remaining moves to be generated - do it incrementally
   // so we get the correct move ordering and flags:
   int count = 0;
   Move m;
   if (board.checkStatus() == InCheck) {
      while ((m=nextEvasion()) != NullMove) {
         split->moves[count++] = m;
      }
   }
   else {
      while ((m=nextMove()) != NullMove) {
         split->moves[count++] = m;
      }
   }
   batch_count = count;
   // technically this is volatile but we are accessing it here
   // pre-split:
   batch = (Move *)split->moves;
   index = 0;
   phase = LAST_PHASE;
   return count;
}


int MoveGenerator::generateAllMoves(Move *moves,int repeatable)
{
   unsigned numMoves  = 0;
   if (board.checkStatus() == InCheck) {
      numMoves = generateEvasions(moves);
   }
   else {
      numMoves += generateCaptures(moves+numMoves);
      numMoves += generateNonCaptures(moves+numMoves);
   }
   if (repeatable) {
      int scores[Constants::MaxMoves];
      for (unsigned i = 0; i < numMoves; i++) {
         scores[i] = (int)StartSquare(moves[i]) + (int)(DestSquare(moves[i]) << 7);
         switch (PromoteTo(moves[i])) {
            case Queen:
               scores[i] &= 0xB000;
               break;
            case Rook:
               scores[i] &= 0x8000;
               break;
            case Bishop:
               scores[i] &= 0x4000;
               break;
            default:
               break;
         }
      }
      sortMoves(moves, scores, numMoves);
   }
   return numMoves;
}


Move MoveGenerator:: nextMove(SplitPoint *s)
{
   if (s) {
      s->lock();
      Move m = nextMove();
      s->unlock();
      return m;
   }
   else
      return nextMove();
}


Move MoveGenerator::nextEvasion(SplitPoint *s)
{
   if (s) {
      s->lock();
      Move m = nextEvasion();
      s->unlock();
      return m;
   }
   else
      return nextEvasion();
}
