// Copyright 2014-2017 by Jon Dart. All Rights Reserved.
#include "tune.h"
#include "chess.h"
#include "attacks.h"
#include "scoring.h"

#include <algorithm>

extern "C" {
#include <math.h>
#include <string.h>
};

#define VAL(x) (Params::PAWN_VALUE*x)

static const score_t MOBILITY_RANGE = VAL(0.333);
static const score_t OUTPOST_RANGE = VAL(0.5);
static const score_t PST_RANGE = VAL(1.0);
static const score_t PP_BLOCK_RANGE = VAL(0.333);
static const score_t TRADE_DOWN_RANGE = VAL(0.333);
static const score_t THREAT_RANGE = VAL(0.75);
static const score_t ENDGAME_KING_POS_RANGE = VAL(0.75);
static const score_t KING_ATTACK_COVER_BOOST_RANGE = Params::KING_ATTACK_FACTOR_RESOLUTION*30;

int Tune::numTuningParams() const
{
   return (int)tune_params.size();
}

int Tune::paramArraySize() const
{
   return (int)SIDE_PROTECTED_PAWN-(int)KING_COVER_BASE+1;
}

#define PARAM(x) tune_params[x].current

static int map_from_pst(int i)
{
   int r = 1+(i/4);
   int f = 1+(i%4);
   ASSERT(OnBoard(MakeSquare(f,r,White)));
   return MakeSquare(f,r,White);
}

static void apply_to_pst(int i,score_t val,score_t arr[])
{
    int r = 1+(i/4);
    int f = 1+(i%4);
    ASSERT(OnBoard(MakeSquare(f,r,White)));
    arr[MakeSquare(f,r,White)] = arr[MakeSquare(9-f,r,White)] = val;
}

static int map_from_outpost(int i)
{
   int r = 5+(i/4);
   int f = 1+(i%4);
   ASSERT(OnBoard(MakeSquare(f,r,White)));
   return MakeSquare(f,r,White);
}

static void apply_to_outpost(int i,int p,int stage,score_t val,score_t arr[2][64][2])
{
   int r = 5+(i/4);
   int f = 1+(i%4);
   ASSERT(OnBoard(MakeSquare(f,r,White)));
   arr[p][MakeSquare(f,r,White)][stage] = arr[p][MakeSquare(9-f,r,White)][stage] = val;
}

Tune::Tune()
{
    static const score_t KING_COVER_RANGE = score_t(0.35*Params::PAWN_VALUE);

    // Tuning params for most parameters (except PSTs, mobility).
    // These are initialized to some reasonable but not optimal values.
    static Tune::TuneParam initial_params[Tune::NUM_MISC_PARAMS] = {
        Tune::TuneParam(Tune::KN_VS_PAWN_ADJUST0,"kn_vs_pawn_adjust0",0,VAL(-0.25),VAL(0.25),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::KN_VS_PAWN_ADJUST1,"kn_vs_pawn_adjust1",VAL(-2.4),VAL(-3.6),VAL(-1.2),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::KN_VS_PAWN_ADJUST2,"kn_vs_pawn_adjust2",VAL(-1.5),VAL(-2.0),VAL(-1.0),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::CASTLING0,"castling0",0,VAL(-0.1),VAL(0.1),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CASTLING1,"castling1",VAL(-0.07),VAL(-0.3),0,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CASTLING2,"castling2",VAL(-0.1),VAL(-0.3),0,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CASTLING3,"castling3",VAL(0.28),VAL(0),VAL(0.5),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CASTLING4,"castling4",VAL(0.2),VAL(0),VAL(0.5),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CASTLING5,"castling5",VAL(-0.28),VAL(-0.5),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_ATTACK_SCALE_MAX,"king_attack_scale_max",VAL(5.0),VAL(3.5),VAL(6.5),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_ATTACK_SCALE_INFLECT,"king_attack_scale_inflect",85,60,120,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_ATTACK_SCALE_FACTOR,"king_attack_scale_factor",54,33,150,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_ATTACK_SCALE_BIAS,"king_attack_scale_bias",VAL(-0.048),VAL(-0.2),0,Tune::TuneParam::Any,0),
        Tune::TuneParam(Tune::KING_COVER1,"king_cover1",VAL(0.05),VAL(0),KING_COVER_RANGE/2,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_COVER2,"king_cover2",VAL(-0.1),-2*KING_COVER_RANGE/3,2*KING_COVER_RANGE/3,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_COVER3,"king_cover3",VAL(-0.15),-KING_COVER_RANGE,0,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_COVER4,"king_cover4",VAL(-0.2),-KING_COVER_RANGE,0,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_FILE_HALF_OPEN,"king_file_half_open",VAL(-0.2),-KING_COVER_RANGE,0,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_FILE_OPEN,"king_file_open",VAL(-0.285),-KING_COVER_RANGE,0,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_COVER_FILE_FACTOR0,"king_cover_file_factor0",64,48,96,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_COVER_FILE_FACTOR1,"king_cover_file_factor1",64,48,96,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_COVER_FILE_FACTOR2,"king_cover_file_factor2",50,32,96,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_COVER_FILE_FACTOR3,"king_cover_file_factor3",40,32,96,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_COVER_BASE,"king_cover_base",VAL(-0.1),-KING_COVER_RANGE,0,Tune::TuneParam::Midgame,0),
        Tune::TuneParam(Tune::KING_DISTANCE_BASIS,"king_distance_basis",VAL(0.312),VAL(0.2),VAL(0.4),Tune::TuneParam::Endgame,0),
        Tune::TuneParam(Tune::KING_DISTANCE_MULT,"king_distance_mult",VAL(0.077),VAL(0.04),VAL(0.12),Tune::TuneParam::Endgame,0),
        Tune::TuneParam(Tune::PIN_MULTIPLIER_MID,"pin_multiplier_mid",VAL(0.227),0,VAL(0.75),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PIN_MULTIPLIER_END,"pin_multiplier_end",VAL(0.289),0,VAL(0.750),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::KRMINOR_VS_R,"krminor_vs_r",VAL(-0.1),VAL(-0.5),VAL(0),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::KRMINOR_VS_R_NO_PAWNS,"krminor_vs_r_no_pawns",VAL(-0.5),VAL(-2.0),VAL(0),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::KQMINOR_VS_Q,"kqminor_vs_q",VAL(-0.1),VAL(-0.5),VAL(0),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::KQMINOR_VS_Q_NO_PAWNS,"kqminor_vs_q_no_pawns",VAL(-0.5),VAL(-3.0),0,Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::MINOR_FOR_PAWNS,"minor_for_pawns",VAL(0.5),0,VAL(0.75),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::ENDGAME_PAWN_ADVANTAGE,"endgame_pawn_advantage",VAL(0.03),VAL(0),VAL(0.25),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::PAWN_ENDGAME1,"pawn_endgame1",VAL(0.3),VAL(0),VAL(0.5),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::PAWN_ENDGAME2,"pawn_endgame2",VAL(0.06),0,VAL(0.5),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::PAWN_ATTACK_FACTOR1,"pawn_attack_factor1",14,0,20,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PAWN_ATTACK_FACTOR2,"pawn_attack_factor2",14,0,20,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::MINOR_ATTACK_FACTOR,"minor_attack_factor",25,10,45,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::MINOR_ATTACK_BOOST,"minor_attack_boost",35,5,70,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ROOK_ATTACK_FACTOR,"rook_attack_factor",31,12,75,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ROOK_ATTACK_BOOST,"rook_attack_boost",34,0,75,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ROOK_ATTACK_BOOST2,"rook_attack_boost2",34,0,75,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::QUEEN_ATTACK_FACTOR,"queen_attack_factor",33,20,75,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::QUEEN_ATTACK_BOOST,"queen_attack_boost",50,0,100,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::QUEEN_ATTACK_BOOST2,"queen_attack_boost2",50,0,100,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_ATTACK_COVER_BOOST_BASE,"king_attack_cover_boost_base",6,4,30,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::KING_ATTACK_COVER_BOOST_SLOPE,"king_attack_cover_boost_slope",140,40,300,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PAWN_THREAT_ON_PIECE_MID,"pawn_threat_on_piece_mid",VAL(0.05),VAL(0),VAL(0.75),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PAWN_THREAT_ON_PIECE_END,"pawn_threat_on_piece_end",VAL(0.05),0,VAL(0.75),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_MM_MID,"piece_threat_mm_mid",VAL(0.1),0,THREAT_RANGE,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_MR_MID,"piece_threat_mr_mid",VAL(0.4),0,THREAT_RANGE,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_MQ_MID,"piece_threat_mq_mid",VAL(0.4),0,THREAT_RANGE,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_MM_END,"piece_threat_mm_end",VAL(0.25),0,THREAT_RANGE,Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_MR_END,"piece_threat_mr_end",VAL(0.5),VAL(0),THREAT_RANGE,Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_MQ_END,"piece_threat_mq_end",VAL(0.5),0,THREAT_RANGE,Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::MINOR_PAWN_THREAT_MID,"minor_pawn_threat_mid",VAL(0.07),0,VAL(0.5),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::MINOR_PAWN_THREAT_END,"minor_pawn_threat_end",VAL(0.15),0,VAL(0.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_RM_MID,"piece_threat_rm_mid",VAL(0.15),0,THREAT_RANGE,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_RR_MID,"piece_threat_rr_mid",VAL(0.15),0,THREAT_RANGE,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_RQ_MID,"piece_threat_rq_mid",VAL(0.5),0,THREAT_RANGE,Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_RM_END,"piece_threat_rm_end",VAL(0.2),0,THREAT_RANGE,Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_RR_END,"piece_threat_rr_end",VAL(0.2),0,THREAT_RANGE,Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PIECE_THREAT_RQ_END,"piece_threat_rq_end",VAL(0.5),0,VAL(0.75),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ROOK_PAWN_THREAT_MID,"rook_pawn_threat_mid",VAL(0.1),0,VAL(0.75),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ROOK_PAWN_THREAT_END,"rook_pawn_threat_end",VAL(0.2),0,VAL(0.75),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ENDGAME_KING_THREAT,"endgame_king_threat",VAL(0.25),0,VAL(0.75),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::BISHOP_TRAPPED,"bishop_trapped",VAL(-1.47),VAL(-2.0),VAL(-0.4)),
        Tune::TuneParam(Tune::BISHOP_PAIR_MID,"bishop_pair_mid",VAL(0.447),VAL(0.1),VAL(0.6),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::BISHOP_PAIR_END,"bishop_pair_end",VAL(0.577),VAL(0.125),VAL(0.75),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::BISHOP_PAWN_PLACEMENT_END,"bishop_pawn_placement_end",VAL(-0.17),VAL(-0.25),0,Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::BAD_BISHOP_MID,"bad_bishop_mid",VAL(-0.04),VAL(-0.15),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::BAD_BISHOP_END,"bad_bishop_end",VAL(-0.06),VAL(-0.15),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::CENTER_PAWN_BLOCK,"center_pawn_block",VAL(-0.2),VAL(-0.35),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::OUTSIDE_PASSER_MID,"outside_passer_mid",VAL(0.11),VAL(0),VAL(0.25),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::OUTSIDE_PASSER_END,"outside_passer_end",VAL(0.25),VAL(0),VAL(0.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::WEAK_PAWN_MID,"weak_pawn_mid",VAL(-0.02),VAL(-0.25),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::WEAK_PAWN_END,"weak_pawn_end",VAL(-0.02),VAL(-0.25),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::WEAK_ON_OPEN_FILE_MID,"weak_on_open_file_mid",VAL(-0.15),VAL(-0.25),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::WEAK_ON_OPEN_FILE_END,"weak_on_open_file_end",VAL(-0.15),VAL(-0.25),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::SPACE,"space",VAL(0.03),VAL(0),VAL(0.12),Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::PAWN_CENTER_SCORE_MID,"pawn_center_score_mid",VAL(0.02),VAL(0),VAL(0.1),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ROOK_ON_7TH_MID,"rook_on_7th_mid",VAL(0.235),VAL(0),VAL(0.8),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ROOK_ON_7TH_END,"rook_on_7th_end",VAL(0.25),VAL(0),VAL(0.8),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::TWO_ROOKS_ON_7TH_MID,"two_rooks_on_7th_mid",VAL(0.25),VAL(0),VAL(0.8),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::TWO_ROOKS_ON_7TH_END,"two_rooks_on_7th_end",VAL(0.25),VAL(0),VAL(0.8),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ROOK_ON_OPEN_FILE_MID,"rook_on_open_file_mid",VAL(0.17),VAL(0),VAL(0.6),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ROOK_ON_OPEN_FILE_END,"rook_on_open_file_end",VAL(0.18),VAL(0),VAL(0.6),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ROOK_BEHIND_PP_MID,"rook_behind_pp_mid",VAL(0.025),0,VAL(0.25),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ROOK_BEHIND_PP_END,"rook_behind_pp_end",VAL(0.07),VAL(0),VAL(0.25),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::QUEEN_OUT,"queen_out",VAL(-0.2),VAL(-0.35),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PAWN_SIDE_BONUS,"pawn_side_bonus",VAL(0.125),VAL(0),VAL(0.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::KING_OWN_PAWN_DISTANCE,"king_own_pawn_distance",VAL(0.075),VAL(0),VAL(0.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::KING_OPP_PAWN_DISTANCE,"king_opp_pawn_distance",VAL(0.02),VAL(0),VAL(0.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::QUEENING_SQUARE_CONTROL_MID,"queening_square_control_mid",VAL(0.5),VAL(0),VAL(0.75),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::QUEENING_SQUARE_CONTROL_END,"queening_square_control_end",VAL(0.5),VAL(0),VAL(0.75),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::QUEENING_SQUARE_OPP_CONTROL_MID,"queening_square_opp_control_mid",VAL(-0.2),VAL(-0.5),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::QUEENING_SQUARE_OPP_CONTROL_END,"queening_square_opp_control_end",VAL(-0.4),VAL(-0.6),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::WRONG_COLOR_BISHOP,"wrong_color_bishop",VAL(0),VAL(-0.5),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::SIDE_PROTECTED_PAWN,"side_protected_pawn",VAL(-0.05),VAL(-0.25),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::KING_POSITION_LOW_MATERIAL0,"king_position_low_material0",250,128,300,Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::KING_POSITION_LOW_MATERIAL1,"king_position_low_material1",225,128,300,Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::KING_POSITION_LOW_MATERIAL2,"king_position_low_material2",130,128,256,Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_MID2,"passed_pawn_mid2",VAL(0.06),VAL(0),VAL(0.3),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_MID3,"passed_pawn_mid3",VAL(0.11),VAL(0),VAL(0.5),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_MID4,"passed_pawn_mid4",VAL(0.18),VAL(0),VAL(0.5),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_MID5,"passed_pawn_mid5",VAL(0.27),VAL(0),VAL(0.75),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_MID6,"passed_pawn_mid6",VAL(0.7),VAL(0.3),VAL(1.0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_MID7,"passed_pawn_mid7",VAL(1.1),VAL(0.5),VAL(1.75),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_END2,"passed_pawn_end2",VAL(0.09),VAL(0),VAL(0.3),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_END3,"passed_pawn_end3",VAL(0.16),VAL(0),VAL(0.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_END4,"passed_pawn_end4",VAL(0.28),VAL(0),VAL(0.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_END5,"passed_pawn_end5",VAL(0.42),VAL(0),VAL(0.75),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_END6,"passed_pawn_end6",VAL(0.84),VAL(0.5),VAL(1.25),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_END7,"passed_pawn_end7",VAL(1.4),VAL(0.5),VAL(1.75),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::PASSED_PAWN_FILE_ADJUST1,"passed_pawn_file_adjust1",64,48,96,Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::PASSED_PAWN_FILE_ADJUST2,"passed_pawn_file_adjust2",64,48,96,Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::PASSED_PAWN_FILE_ADJUST3,"passed_pawn_file_adjust3",64,48,96,Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::PASSED_PAWN_FILE_ADJUST4,"passed_pawn_file_adjust4",64,48,80,Tune::TuneParam::Any,1),
        Tune::TuneParam(Tune::POTENTIAL_PASSER_MID2,"potential_passer_mid2",VAL(0.026),VAL(0),VAL(0.2),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::POTENTIAL_PASSER_MID3,"potential_passer_mid3",VAL(0.037),VAL(0),VAL(0.25),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::POTENTIAL_PASSER_MID4,"potential_passer_mid4",VAL(0.075),VAL(0),VAL(0.3),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::POTENTIAL_PASSER_MID5,"potential_passer_mid5",VAL(0.075),VAL(0),VAL(0.5),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::POTENTIAL_PASSER_MID6,"potential_passer_mid6",VAL(0.236),VAL(0),VAL(0.75),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::POTENTIAL_PASSER_END2,"potential_passer_end2",VAL(0.04),VAL(0),VAL(0.2),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::POTENTIAL_PASSER_END3,"potential_passer_end3",VAL(0.056),VAL(0),VAL(0.25),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::POTENTIAL_PASSER_END4,"potential_passer_end4",VAL(0.115),VAL(0),VAL(0.3),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::POTENTIAL_PASSER_END5,"potential_passer_end5",VAL(0.115),VAL(0),VAL(0.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::POTENTIAL_PASSER_END6,"potential_passer_end6",VAL(0.36),VAL(0),VAL(0.75),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_MID2,"connected_passer_mid2",VAL(0),VAL(0),VAL(0.25),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_MID3,"connected_passer_mid3",VAL(0.08),VAL(0),VAL(0.3),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_MID4,"connected_passer_mid4",VAL(0.2),VAL(0),VAL(0.4),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_MID5,"connected_passer_mid5",VAL(0.227),VAL(0),VAL(0.75),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_MID6,"connected_passer_mid6",VAL(0.5),VAL(0.1),VAL(1.75),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_MID7,"connected_passer_mid7",VAL(0.77),VAL(0.25),VAL(2.25),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_END2,"connected_passer_end2",VAL(0),VAL(0),VAL(0.25),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_END3,"connected_passer_end3",VAL(0.08),VAL(0),VAL(0.3),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_END4,"connected_passer_end4",VAL(0.2),VAL(0),VAL(0.4),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_END5,"connected_passer_end5",VAL(0.227),VAL(0),VAL(1.0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_END6,"connected_passer_end6",VAL(0.5),VAL(0.1),VAL(2.0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::CONNECTED_PASSER_END7,"connected_passer_end7",VAL(0.77),VAL(0.25),VAL(2.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_MID2,"adjacent_passer_mid2",VAL(0),VAL(0),VAL(0.25),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_MID3,"adjacent_passer_mid3",VAL(0.1),VAL(0),VAL(0.3),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_MID4,"adjacent_passer_mid4",VAL(0.15),VAL(0),VAL(0.4),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_MID5,"adjacent_passer_mid5",VAL(0.15),VAL(0),VAL(0.5),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_MID6,"adjacent_passer_mid6",VAL(0.3),VAL(0),VAL(1.0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_MID7,"adjacent_passer_mid7",VAL(0.7),VAL(0.15),VAL(1.5),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_END2,"adjacent_passer_end2",VAL(0),VAL(0),VAL(0.25),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_END3,"adjacent_passer_end3",VAL(0.1),VAL(0),VAL(0.3),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_END4,"adjacent_passer_end4",VAL(0.15),VAL(0),VAL(0.4),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_END5,"adjacent_passer_end5",VAL(0.15),VAL(0),VAL(0.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_END6,"adjacent_passer_end6",VAL(0.3),VAL(0),VAL(1.0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ADJACENT_PASSER_END7,"adjacent_passer_end7",VAL(0.7),VAL(0.15),VAL(1.5),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::DOUBLED_PAWNS_MID1,"doubled_pawns_mid1",VAL(-0.05),VAL(-0.5),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::DOUBLED_PAWNS_MID2,"doubled_pawns_mid2",VAL(-0.06),VAL(-0.5),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::DOUBLED_PAWNS_MID3,"doubled_pawns_mid3",VAL(-0.08),VAL(-0.5),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::DOUBLED_PAWNS_MID4,"doubled_pawns_mid4",VAL(-0.1),VAL(-0.5),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::DOUBLED_PAWNS_END1,"doubled_pawns_end1",VAL(-0.05),VAL(-0.5),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::DOUBLED_PAWNS_END2,"doubled_pawns_end2",VAL(-0.06),VAL(-0.5),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::DOUBLED_PAWNS_END3,"doubled_pawns_end3",VAL(-0.08),VAL(-0.5),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::DOUBLED_PAWNS_END4,"doubled_pawns_end4",VAL(-0.1),VAL(-0.5),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::TRIPLED_PAWNS_MID1,"tripled_pawns_mid1",VAL(-0.07),VAL(-0.5),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::TRIPLED_PAWNS_MID2,"tripled_pawns_mid2",VAL(-0.1),VAL(-0.5),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::TRIPLED_PAWNS_MID3,"tripled_pawns_mid3",VAL(-0.16),VAL(-0.5),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::TRIPLED_PAWNS_MID4,"tripled_pawns_mid4",VAL(-0.2),VAL(-0.5),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::TRIPLED_PAWNS_END1,"tripled_pawns_end1",VAL(-0.07),VAL(-0.5),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::TRIPLED_PAWNS_END2,"tripled_pawns_end2",VAL(-0.1),VAL(-0.5),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::TRIPLED_PAWNS_END3,"tripled_pawns_end3",VAL(-0.16),VAL(-0.5),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::TRIPLED_PAWNS_END4,"tripled_pawns_end4",VAL(-0.2),VAL(-0.5),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ISOLATED_PAWN_MID1,"isolated_pawn_mid1",VAL(-0.07),VAL(-0.25),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ISOLATED_PAWN_MID2,"isolated_pawn_mid2",VAL(-0.07),VAL(-0.25),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ISOLATED_PAWN_MID3,"isolated_pawn_mid3",VAL(-0.07),VAL(-0.25),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ISOLATED_PAWN_MID4,"isolated_pawn_mid4",VAL(-0.1),VAL(-0.25),VAL(0),Tune::TuneParam::Midgame,1),
        Tune::TuneParam(Tune::ISOLATED_PAWN_END1,"isolated_pawn_end1",VAL(-0.07),VAL(-0.25),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ISOLATED_PAWN_END2,"isolated_pawn_end2",VAL(-0.07),VAL(-0.25),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ISOLATED_PAWN_END3,"isolated_pawn_end3",VAL(-0.07),VAL(-0.25),VAL(0),Tune::TuneParam::Endgame,1),
        Tune::TuneParam(Tune::ISOLATED_PAWN_END4,"isolated_pawn_end4",VAL(-0.1),VAL(-0.25),VAL(0),Tune::TuneParam::Endgame,1)
    };

   // boostrap values - maybe not optimal
   static const score_t KNIGHT_OUTPOST_INIT[64] =
      {0 ,0 ,0 ,0 ,0 ,0 ,0 ,0,
       0 ,0 ,0 ,0 ,0 ,0 ,0 ,0,
       0 ,0 ,0 ,0 ,0 ,0 ,0 ,0,
       VAL(0.01), VAL(0.03), VAL(0.06), VAL(0.06), VAL(0.06), VAL(0.06), VAL(0.03), VAL(0.01),
       VAL(0.01), VAL(0.04), VAL(0.09), VAL(0.140), VAL(0.140), VAL(0.090), VAL(0.04),VAL(0.01),
       VAL(0.01), VAL(0.04), VAL(0.09),VAL(0.140), VAL(0.140),VAL(0.09),VAL(0.04), VAL(0.01),
       VAL(0.01), VAL(0.03), VAL(0.06), VAL(0.06), VAL(0.06), VAL(0.06), VAL(0.03), VAL(0.01),
       VAL(0.01), VAL(0.01), VAL(0.01), VAL(0.01), VAL(0.01), VAL(0.01), VAL(0.01), VAL(0.01)
      };

   static const score_t BISHOP_OUTPOST_INIT[64] = 
      {0 ,0 ,0 ,0 ,0 ,0 ,0 ,0,
       0 ,0 ,0 ,0 ,0 ,0 ,0 ,0,
       0 ,0 ,0 ,0 ,0 ,0 ,0 ,0,
       VAL(0.01), VAL(0.02), VAL(0.05), VAL(0.05), VAL(0.05), VAL(0.05), VAL(0.02), VAL(0.10),
       VAL(0.01), VAL(0.03), VAL(0.07), VAL(0.110), VAL(0.110), VAL(0.07), VAL(0.03), VAL(0.01),
       VAL(0.01), VAL(0.03), VAL(0.07), VAL(0.110), VAL(0.110), VAL(0.07), VAL(0.03), VAL(0.01),
       VAL(0.01), VAL(0.01), VAL(0.05), VAL(0.05), VAL(0.05), VAL(0.05), VAL(0.01), VAL(0.10),
       VAL(0.01), VAL(0.01), VAL(0.01), VAL(0.01), VAL(0.01), VAL(0.01), VAL(0.01), VAL(0.01)
      };

   static const score_t TRADE_DOWN_INIT[16] = {VAL(0.688), VAL(0.645), VAL(0.602), VAL(0.559), VAL(0.516), VAL(0.473), VAL(0.430), VAL(0.387), VAL(0.344), VAL(0.301), VAL(0.258), VAL(0.215), VAL(0.172), VAL(0.129), VAL(0.086), 0};
   static const score_t KNIGHT_MOBILITY_INIT[9] = {VAL(-0.180), VAL(-0.07), VAL(-0.02), 0, VAL(0.02), VAL(0.05), VAL(0.07), VAL(0.1), VAL(0.12)};
   static const score_t BISHOP_MOBILITY_INIT[15] = {VAL(-0.2), VAL(-0.11), VAL(-.07), -VAL(0.03), 0, VAL(0.03), VAL(0.06), VAL(0.09), VAL(0.09), VAL(0.09), VAL(0.09), VAL(0.09), VAL(0.09), VAL(0.09), VAL(0.09)};
   static const score_t ROOK_MOBILITY_INIT[2][15] = {{VAL(-0.22), VAL(-0.12), VAL(-0.08), VAL(-0.03), 0, VAL(0.03), VAL(0.07), VAL(0.1), VAL(0.12), VAL(0.14), VAL(0.17), VAL(0.19), VAL(0.21), VAL(0.23), VAL(0.24)}, {VAL(-0.3), VAL(-0.17), VAL(-0.11), VAL(-0.05), 0, VAL(0.05), VAL(0.09), VAL(0.14), VAL(0.17), VAL(0.2), VAL(0.23), VAL(0.26), VAL(0.29), VAL(0.31), VAL(0.32)}};
   static const score_t QUEEN_MOBILITY_INIT[2][29] = {{VAL(-0.1), VAL(-0.05), VAL(-0.01), VAL(-0.01), VAL(0.04), VAL(0.07), VAL(0.09), VAL(0.11), VAL(0.13), VAL(0.14), VAL(0.16), VAL(0.17), VAL(0.19), VAL(0.2), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21), VAL(0.21)}, {VAL(-0.12), VAL(-0.06), VAL(-0.01), VAL(0.01), VAL(0.05), VAL(0.08), VAL(0.11), VAL(0.13), VAL(0.16), VAL(0.17), VAL(0.2), VAL(0.21), VAL(0.23), VAL(0.25), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26), VAL(0.26)}};
   static const score_t KING_MOBILITY_ENDGAME_INIT[5] = {VAL(-0.2), VAL(-0.12), VAL(-0.06), 0, VAL(0.01)};

   static const score_t KNIGHT_PST_INIT[2][64] =
       {
          {VAL(-0.22), VAL(-0.14), VAL(-0.11), VAL(-0.1), VAL(-0.1), VAL(-0.11), VAL(-0.14), VAL(-0.22),
           VAL(-0.15), VAL(-0.06), VAL(-0.04), VAL(-0.03), VAL(-0.03), VAL(-0.04), VAL(-0.06), VAL(-0.015),
           VAL(-0.12), VAL(-0.04), VAL(-0.01), VAL(0), VAL(0), VAL(-0.01), VAL(-0.04), VAL(-0.12),
           VAL(-0.11), VAL(-0.03), VAL(0.0), VAL(0.03), VAL(0.03), VAL(0.0), VAL(-0.0), VAL(-0.11),
           VAL(-0.11), VAL(-0.03), VAL(0.0), VAL(0.03), VAL(0.03), VAL(0.0), VAL(-0.0), VAL(-0.11),
           VAL(-0.12), VAL(-0.04), VAL(-0.01), VAL(0.0), VAL(0.0), VAL(-0.01), VAL(-0.04), VAL(-0.12),
           VAL(-0.15), VAL(-0.06), VAL(-0.04), VAL(-0.03), VAL(-0.03), VAL(-0.04), VAL(-0.06), VAL(-0.15),
           VAL(-0.18), VAL(-0.09), VAL(-0.07), VAL(-0.06), VAL(-0.06), VAL(-0.07), VAL(-0.09), VAL(-0.18)
           }, 
           {VAL(-0.230), VAL(-0.190), VAL(-0.160), VAL(-0.150), VAL(-0.150), VAL(-0.160), VAL(-0.190), VAL(-0.230),
            VAL(-0.130), VAL(-0.90), VAL(-0.50), VAL(-0.40), VAL(-0.40),  VAL(-0.50), VAL(-0.90), VAL(-0.130),
            VAL(-0.90), VAL(-0.50), VAL(-0.20), VAL(-0.10), VAL(-0.10), VAL(-0.20), VAL(-0.50), VAL(-0.90),
            VAL(-0.80), VAL(-0.40), VAL(-0.10), VAL(0.0), VAL(0.0), VAL(-0.10), VAL(-0.40), VAL(-0.80),
            VAL(-0.80), VAL(-0.30), VAL(0.0), VAL(0.010), VAL(0.010), VAL(0.0), VAL(-0.30), VAL(-0.80),
            VAL(-0.90), VAL(-0.40), VAL(0.0), VAL(0.0), VAL(0.0), VAL(0.0), VAL(-0.40), VAL(-0.90),
            VAL(-0.130), VAL(-0.70), VAL(-0.40), VAL(-0.30), VAL(-0.30), VAL(-0.40), VAL(-0.70), VAL(-0.130),
            VAL(-0.170), VAL(-0.130), VAL(-0.90), VAL(-0.80), VAL(-0.80), VAL(-0.90), VAL(-0.130), VAL(-0.170)
           }
       };

   static const score_t BISHOP_PST_INIT[2][64] =
       {
           {VAL(-0.225), VAL(-0.120 ), VAL(-0.155 ), VAL(-0.155 ), VAL(-0.155 ), VAL(-0.155 ), VAL(-0.120 ), VAL(-0.225),
            VAL(-0.10 ), VAL(0.08), VAL(0.0), VAL(0.06), VAL(0.06), VAL(0.0), VAL(0.08), VAL(-0.01),
            VAL(-0.10 ), VAL(0.0), VAL(0.06), VAL(0.08), VAL(0.08), VAL(0.06), VAL(0.0), VAL(-0.10),
            VAL(0.0), VAL(0.0), VAL(0.06), VAL(0.100), VAL(0.100), VAL(0.06), VAL(0.0), VAL(0.0),
            VAL(0.0), VAL(0.06), VAL(0.06), VAL(0.06), VAL(0.06), VAL(0.06), VAL(0.06), VAL(0.0),
            VAL(0.100), VAL(0.100), VAL(0.100), VAL(0.100), VAL(0.100), VAL(0.100), VAL(0.100), VAL(0.100),
            VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100),
            VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100 ), VAL(-0.100)
           },
           { VAL(-0.75), VAL(-0.45), VAL(-0.005), VAL(0.045), VAL(0.045), VAL(-0.005), VAL(-0.45), VAL(-0.75),
             VAL(-0.45), VAL(-0.15), VAL(0.025), VAL(0.075), VAL(0.075), VAL(0.025), VAL(-0.15), VAL(-0.45),
             VAL(-0.005), VAL(0.025), VAL(0.065), VAL(0.115), VAL(0.115), VAL(0.065), VAL(0.025), VAL(-0.005),
             VAL(0.045), VAL(0.075), VAL(0.115), VAL(0.165), VAL(0.165), VAL(0.115), VAL(0.075), VAL(0.045),
             VAL(0.045), VAL(0.075), VAL(0.115), VAL(0.165), VAL(0.165), VAL(0.115), VAL(0.075), VAL(0.045),
             VAL(-0.005), VAL(0.025), VAL(0.065), VAL(0.115), VAL(0.115), VAL(0.065), VAL(0.025), VAL(-0.005),
             VAL(-0.45), VAL(-0.15), VAL(0.025), VAL(0.075), VAL(0.075), VAL(0.025), VAL(-0.15), VAL(-0.45),
             VAL(-0.75), VAL(-0.45), VAL(-0.005), 45, 45, VAL(-0.005), VAL(-0.45), VAL(-0.75)
           }
       };

   static const score_t KING_PST_INIT[2][64] = {
       {0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        VAL(-0.06), VAL(-0.06), VAL(-0.06), VAL(-0.06), VAL(-0.06), VAL(-0.06), VAL(-0.06), VAL(-0.06),
        VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36),
        VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36),
        VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36),
        VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36),
        VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36), VAL(-0.36)
       },
       {VAL(-0.280), VAL(-0.230), VAL(-0.180), VAL(-0.130), VAL(-0.130), VAL(-0.180), VAL(-0.230), VAL(-0.280),
        VAL(-0.220), VAL(-0.170), VAL(-0.120), VAL(-0.070), VAL(-0.070), VAL(-0.120), VAL(-0.170), VAL(-0.220),
        VAL(-0.160), VAL(-0.110), VAL(-0.06), VAL(-0.010), VAL(-0.010), VAL(-0.06), VAL(-0.110), VAL(-0.160),
        VAL(-0.100), VAL(-0.050), VAL(0.0), VAL(0.050), VAL(0.050), VAL(0.0), VAL(-0.050), VAL(-0.100),
        VAL(-0.40), VAL(0.010), VAL(0.060), VAL(0.110), VAL(0.110), VAL(0.060), VAL(0.010), VAL(-0.40),
        VAL(0.020), VAL(0.070), VAL(0.120), VAL(0.170), VAL(0.170), VAL(0.120), VAL(0.070), VAL(0.020),
        VAL(0.080), VAL(0.130), VAL(0.180), VAL(0.230), VAL(0.230), VAL(0.180), VAL(0.130), VAL(0.080),
        VAL(0.080), VAL(0.130), VAL(0.180), VAL(0.230), VAL(0.230), VAL(0.180), VAL(0.130), VAL(0.080)
       }
   };

   const score_t PP_OWN_PIECE_BLOCK_INIT[2][21] = {{VAL(-0.015),VAL(-0.015),VAL(-0.015),VAL(-0.015),VAL(-0.015),VAL(-0.015),VAL(-0.030),VAL(-0.030),VAL(-0.030),VAL(-0.030),VAL(-0.030),VAL(-0.045),VAL(-0.045),VAL(-0.045),VAL(-0.045),VAL(-0.060),VAL(-0.060),VAL(-0.060),VAL(-0.075),VAL(-0.075),VAL(-0.090)},{VAL(-0.043),VAL(-0.043),VAL(-0.043),VAL(-0.043),VAL(-0.043),VAL(-0.043),VAL(-0.086),VAL(-0.086),VAL(-0.086),VAL(-0.086),VAL(-0.086),VAL(-0.129),VAL(-0.129),VAL(-0.129),VAL(-0.129),VAL(-0.172),VAL(-0.172),VAL(-0.172),VAL(-0.215),VAL(-0.215),VAL(-0.258)}};
   const score_t PP_OPP_PIECE_BLOCK_INIT[2][21] = {{VAL(-0.171),VAL(-0.085),VAL(-0.057),VAL(-0.042),VAL(-0.034),VAL(-0.028),VAL(-0.190),VAL(-0.095),VAL(-0.063),VAL(-0.047),VAL(-0.038),VAL(-0.217),VAL(-0.108),VAL(-0.072),VAL(-0.54),VAL(-0.251),VAL(-0.125),VAL(-0.083),VAL(-0.361),VAL(-0.180),VAL(-0.569)},{VAL(-0.147),VAL(-0.147),VAL(-0.147),VAL(-0.147),VAL(-0.147),VAL(-0.147),VAL(-0.159),VAL(-0.159),VAL(-0.159),VAL(-0.159),VAL(-0.159),VAL(-0.180),VAL(-0.180),VAL(-0.180),VAL(-0.180),VAL(-0.204),VAL(-0.204),VAL(-0.204),VAL(-0.276),VAL(-0.276),VAL(-0.374)}};

   int i = 0;
   for (;i < NUM_MISC_PARAMS; i++) {
      tune_params.push_back(initial_params[i]);
   }
   static const TuneParam::Scaling scales[2] = {Tune::TuneParam::Midgame,
                                                Tune::TuneParam::Endgame};
   ASSERT(i==KING_OPP_PASSER_DISTANCE);
   for (int x = 0; x < 6; x++) {
      stringstream name;
      name << "king_opp_passer_distance_rank" << x+2;
      tune_params.push_back(TuneParam(i++,name.str(),10+x*10,0,ENDGAME_KING_POS_RANGE,Tune::TuneParam::Endgame,1));
   }
   ASSERT(i==PP_OWN_PIECE_BLOCK_MID);
   // add passed pawn block tables
   for (int phase = 0; phase < 2; phase++) {
      for (int x = 0; x < 21; x++) {
         stringstream name;
         name << "pp_own_piece_block";
         if (phase == 0)
            name << "_mid";
         else
            name << "_end";
         name << x;
         score_t val = PP_OWN_PIECE_BLOCK_INIT[phase][x];
         tune_params.push_back(TuneParam(i++,name.str(),val,-PP_BLOCK_RANGE,0,scales[phase],1));
      }
   }
   for (int phase = 0; phase < 2; phase++) {
      for (int x = 0; x < 21; x++) {
         stringstream name;
         name << "pp_opp_piece_block";
         if (phase == 0)
            name << "_mid";
         else
            name << "_end";
         name << x;
         score_t val = PP_OPP_PIECE_BLOCK_INIT[phase][x];
         tune_params.push_back(TuneParam(i++,name.str(),val,-PP_BLOCK_RANGE,0,scales[phase],1));
      }
   }

   static const string names[] =
      {"knight_pst","bishop_pst","rook_pst","queen_pst","king_pst"};
   // add PSTs
   ASSERT(i == KNIGHT_PST_MIDGAME);
   for (int n = 0; n < 5; n++) {
      for (int phase = 0; phase < 2; phase++) {
         for (int j = 0; j < 32; j++) {
            stringstream name;
            name << names[n];
            if (phase == 0)
               name << "_mid";
            else
               name << "_end";
            name << j;
            score_t val = 0;
            ASSERT(map_from_pst(j) >= 0 && map_from_pst(j) < 64);
            switch(n) {
            case 0:
               val = KNIGHT_PST_INIT[phase][map_from_pst(j)]; break;
            case 1:
               val = BISHOP_PST_INIT[phase][map_from_pst(j)]; break;
            case 2:
            case 3:
               val = (score_t)0; break;
            case 4:
               val = KING_PST_INIT[phase][map_from_pst(j)]; break;
            default:
               break;
            }
            tune_params.push_back(TuneParam(i++,name.str(),val,-PST_RANGE,PST_RANGE,scales[phase],1));
         }
      }
   }
   // add mobility
   ASSERT(i==KNIGHT_MOBILITY);
   for (int m = 0; m < 9; m++) {
      stringstream name;
      name << "knight_mobility" << m;
      const score_t val = KNIGHT_MOBILITY_INIT[m];
      tune_params.push_back(TuneParam(i++,name.str(),val,val-MOBILITY_RANGE,val+MOBILITY_RANGE,Tune::TuneParam::Any,1));
   }
   ASSERT(i==BISHOP_MOBILITY);
   for (int m = 0; m < 15; m++) {
      stringstream name;
      name << "bishop_mobility" << m;
      tune_params.push_back(TuneParam(i++,name.str(),BISHOP_MOBILITY_INIT[m],-MOBILITY_RANGE,MOBILITY_RANGE,Tune::TuneParam::Any,1));
   }
   ASSERT(i==ROOK_MOBILITY_MIDGAME);
   for (int phase = 0; phase < 2; phase++) {
      for (int m = 0; m < 15; m++) {
         stringstream name;
         name << "rook_mobility";
         if (phase == 0) {
            name << "_mid";
         }
         else {
            name << "_end";
         }
         name << m;
         tune_params.push_back(TuneParam(i++,name.str(),ROOK_MOBILITY_INIT[phase][m],-MOBILITY_RANGE,MOBILITY_RANGE,scales[phase],1));
      }
   }
   ASSERT(i==QUEEN_MOBILITY_MIDGAME);
   for (int phase = 0; phase < 2; phase++) {
      for (int m = 0; m < 24; m++) {
         stringstream name;
         name << "queen_mobility";
         if (phase == 0) {
            name << "_mid";
         }
         else {
            name << "_end";
         }
         name << m;
         tune_params.push_back(TuneParam(i++,name.str(),QUEEN_MOBILITY_INIT[phase][m],-MOBILITY_RANGE,MOBILITY_RANGE,scales[phase],1));
      }
   }
   ASSERT(i==KING_MOBILITY_ENDGAME);
   for (int m = 0; m < 5; m++) {
      stringstream name;
      name << "king_mobility_endgame" << m;
      tune_params.push_back(TuneParam(i++,name.str(),KING_MOBILITY_ENDGAME_INIT[m],-MOBILITY_RANGE,MOBILITY_RANGE,Tune::TuneParam::Endgame,1));
   }
   // outposts
   ASSERT(i==KNIGHT_OUTPOST);
   for (int p = 0; p < 2; p++) {
      for (int s = 0; s < 16; s++) {
          for (int stage = 0; stage < 2; stage++) {
              stringstream name;
              name << "knight_outpost" << p << '_' << s << "_" << (stage == 0 ? "mid" : "end");
              score_t val = KNIGHT_OUTPOST_INIT[map_from_outpost(s)];
              if (p == 0) val /= 2;
              tune_params.push_back(TuneParam(i++,name.str(),val,0,OUTPOST_RANGE,
                                              stage == 0 ? Tune::TuneParam::Midgame : Tune::TuneParam::Endgame,1));
          }
      }
   }
   ASSERT(i==BISHOP_OUTPOST);
   for (int p = 0; p < 2; p++) {
      for (int s = 0; s < 16; s++) {
          for (int stage = 0; stage < 2; stage++) {
              stringstream name;
              name << "bishop_outpost" << p << '_' << s << "_" << (stage == 0 ? "mid" : "end");
              score_t val = BISHOP_OUTPOST_INIT[map_from_outpost(s)];
              if (p == 0) val /= 2;
              tune_params.push_back(TuneParam(i++,name.str(),val,0,OUTPOST_RANGE,
                                              stage == 0 ? Tune::TuneParam::Midgame : Tune::TuneParam::Endgame,1));
          }
      }
   }
   // add trade down
   ASSERT(i==TRADE_DOWN);
   for (int p = 0; p < 8; p++) {
      stringstream name;
      name << "trade_down" << p;
      score_t val = TRADE_DOWN_INIT[p];
      tune_params.push_back(TuneParam(i++,name.str(),val,std::max<score_t>(0,val-TRADE_DOWN_RANGE),val+TRADE_DOWN_RANGE,Tune::TuneParam::Any,1));
   }
   ASSERT(i==RB_ADJUST);
   for (int p = 0; p < 6; p++) {
      stringstream name;
      name << "rb_adjust" << p;
      score_t val = score_t(-0.35*Params::PAWN_VALUE)+ (p>0 ? Params::PAWN_VALUE/4: 0) + 
          score_t(0.05*Params::PAWN_VALUE)*p;
      tune_params.push_back(TuneParam(i++,name.str(),val,val-Params::PAWN_VALUE,val+Params::PAWN_VALUE,Tune::TuneParam::Any,1));
   }
   ASSERT(i==RBN_ADJUST);
   for (int p = 0; p < 6; p++) {
      stringstream name;
      name << "rbn_adjust" << p;
      score_t val = -score_t(0.15*Params::PAWN_VALUE)*p;
      tune_params.push_back(TuneParam(i++,name.str(),val,val-Params::PAWN_VALUE/2,val+Params::PAWN_VALUE/2,Tune::TuneParam::Any,1));
   }
   ASSERT(i==QR_ADJUST);
   for (int p = 0; p < 5; p++) {
      stringstream name;
      name << "qr_adjust" << p;
      score_t init[5] = {0.350*Params::PAWN_VALUE, Params::PAWN_VALUE, 0.9*Params::PAWN_VALUE, 0.3*Params::PAWN_VALUE, 0};
      score_t val = init[p];
      tune_params.push_back(TuneParam(i++,name.str(),val,val-score_t(0.75*Params::PAWN_VALUE),val+score_t(0.75*Params::PAWN_VALUE),Tune::TuneParam::Any,1));
   }
}

void Tune::checkParams() const
{
#ifdef _DEBUG
   if (NUM_MISC_PARAMS != KING_OPP_PASSER_DISTANCE) {
      cerr << "warning: NUM_MISC_PARAMS incorrect, should be " << KING_OPP_PASSER_DISTANCE << endl;
   }
   for (int i = 0; i<NUM_MISC_PARAMS; i++) {
      if (tune_params[i].index != i)
         cerr << "warning: index mismatch in Tune::tune_params at position " << i << ", param " << tune_params[i].name << endl;
      if (tune_params[i].current < tune_params[i].min_value) {
         cerr << "warning: param " << tune_params[i].name << " has current < min" << endl;
      }
      if (tune_params[i].current > tune_params[i].max_value) {
         cerr << "warning: param " << tune_params[i].name << " has current > max" << endl;
      }
      if (tune_params[i].min_value > tune_params[i].max_value) {
         cerr << "warning: param " << tune_params[i].name << " has min>max" << endl;
      }
/*
      if (tune_params[i].min_value == tune_params[i].current) {
         cerr << "warning: param " << tune_params[i].name << " tuned to min value (" << tune_params[i].current << ")." << endl;
      }
      if (tune_params[i].max_value == tune_params[i].current) {
         cerr << "warning: param " << tune_params[i].name << " tuned to max value (" << tune_params[i].current << ")." << endl;
      }
*/
   }
#endif
}

void Tune::applyParams() const
{
   checkParams();

   score_t *dest = Params::KN_VS_PAWN_ADJUST;
   int i, j = 0;
   for (i = 0; i < 3; i++) {
      *dest++ = Tune::tune_params[j++].current;
   }
   dest = Params::CASTLING;
   for (i = 0; i < 6; i++) {
      *dest++ = Tune::tune_params[j++].current;
   }
   Params::KING_ATTACK_SCALE_MAX = tune_params[Tune::KING_ATTACK_SCALE_MAX].current;
   Params::KING_ATTACK_SCALE_INFLECT = tune_params[Tune::KING_ATTACK_SCALE_INFLECT].current;
   Params::KING_ATTACK_SCALE_FACTOR = tune_params[Tune::KING_ATTACK_SCALE_FACTOR].current;
   Params::KING_ATTACK_SCALE_BIAS = tune_params[Tune::KING_ATTACK_SCALE_BIAS].current;

   // compute king cover scores
   for (i = 0; i < 6; i++) {
      for (int k = 0; k < 4; k++) {
         Params::KING_COVER[i][k] =
            tune_params[Tune::KING_COVER1+i].current*tune_params[Tune::KING_COVER_FILE_FACTOR0+k].current/64;
      }
   }
   Params::KING_COVER_BASE = tune_params[KING_COVER_BASE].current;
   Params::KING_DISTANCE_BASIS = tune_params[KING_DISTANCE_BASIS].current;
   Params::KING_DISTANCE_MULT = tune_params[KING_DISTANCE_MULT].current;
   Params::PIN_MULTIPLIER_MID = tune_params[PIN_MULTIPLIER_MID].current;
   Params::PIN_MULTIPLIER_END = tune_params[PIN_MULTIPLIER_END].current;
   Params::KRMINOR_VS_R = tune_params[KRMINOR_VS_R].current;
   Params::KRMINOR_VS_R_NO_PAWNS = tune_params[KRMINOR_VS_R_NO_PAWNS].current;
   Params::KQMINOR_VS_Q = tune_params[KQMINOR_VS_Q].current;
   Params::KQMINOR_VS_Q_NO_PAWNS = tune_params[KQMINOR_VS_Q_NO_PAWNS].current;
   Params::MINOR_FOR_PAWNS = tune_params[MINOR_FOR_PAWNS].current;
   Params::ENDGAME_PAWN_ADVANTAGE = tune_params[ENDGAME_PAWN_ADVANTAGE].current;
   Params::PAWN_ENDGAME1 = tune_params[PAWN_ENDGAME1].current;
   Params::PAWN_ENDGAME2 = tune_params[PAWN_ENDGAME2].current;
   Params::PAWN_ATTACK_FACTOR1 = tune_params[PAWN_ATTACK_FACTOR1].current;
   Params::PAWN_ATTACK_FACTOR2 = tune_params[PAWN_ATTACK_FACTOR2].current;
   Params::MINOR_ATTACK_FACTOR = tune_params[MINOR_ATTACK_FACTOR].current;
   Params::MINOR_ATTACK_BOOST = tune_params[MINOR_ATTACK_BOOST].current;
   Params::ROOK_ATTACK_FACTOR = tune_params[ROOK_ATTACK_FACTOR].current;
   Params::ROOK_ATTACK_BOOST = tune_params[ROOK_ATTACK_BOOST].current;
   Params::ROOK_ATTACK_BOOST2 = tune_params[ROOK_ATTACK_BOOST2].current;
   Params::KING_ATTACK_COVER_BOOST_BASE = tune_params[KING_ATTACK_COVER_BOOST_BASE].current;
   Params::KING_ATTACK_COVER_BOOST_SLOPE = tune_params[KING_ATTACK_COVER_BOOST_SLOPE].current;
   Params::QUEEN_ATTACK_FACTOR = tune_params[QUEEN_ATTACK_FACTOR].current;
   Params::QUEEN_ATTACK_BOOST = tune_params[QUEEN_ATTACK_BOOST].current;
   Params::QUEEN_ATTACK_BOOST2 = tune_params[QUEEN_ATTACK_BOOST2].current;
   Params::PAWN_THREAT_ON_PIECE_MID = tune_params[PAWN_THREAT_ON_PIECE_MID].current;
   Params::PAWN_THREAT_ON_PIECE_END = tune_params[PAWN_THREAT_ON_PIECE_END].current;
   Params::PIECE_THREAT_MM_MID = tune_params[PIECE_THREAT_MM_MID].current;
   Params::PIECE_THREAT_MR_MID = tune_params[PIECE_THREAT_MR_MID].current;
   Params::PIECE_THREAT_MQ_MID = tune_params[PIECE_THREAT_MQ_MID].current;
   Params::PIECE_THREAT_MM_END = tune_params[PIECE_THREAT_MM_END].current;
   Params::PIECE_THREAT_MR_END = tune_params[PIECE_THREAT_MR_END].current;
   Params::PIECE_THREAT_MQ_END = tune_params[PIECE_THREAT_MQ_END].current;
   Params::MINOR_PAWN_THREAT_MID = tune_params[MINOR_PAWN_THREAT_MID].current;
   Params::MINOR_PAWN_THREAT_END = tune_params[MINOR_PAWN_THREAT_END].current;
   Params::PIECE_THREAT_RM_MID = tune_params[PIECE_THREAT_RM_MID].current;
   Params::PIECE_THREAT_RR_MID = tune_params[PIECE_THREAT_RR_MID].current;
   Params::PIECE_THREAT_RQ_MID = tune_params[PIECE_THREAT_RQ_MID].current;
   Params::PIECE_THREAT_RM_END = tune_params[PIECE_THREAT_RM_END].current;
   Params::PIECE_THREAT_RR_END = tune_params[PIECE_THREAT_RR_END].current;
   Params::PIECE_THREAT_RQ_END = tune_params[PIECE_THREAT_RQ_END].current;
   Params::ROOK_PAWN_THREAT_MID = tune_params[ROOK_PAWN_THREAT_MID].current;
   Params::ROOK_PAWN_THREAT_END = tune_params[ROOK_PAWN_THREAT_END].current;
   Params::ENDGAME_KING_THREAT = tune_params[ENDGAME_KING_THREAT].current;
   Params::BISHOP_TRAPPED = tune_params[BISHOP_TRAPPED].current;
   Params::BISHOP_PAIR_MID = tune_params[BISHOP_PAIR_MID].current;
   Params::BISHOP_PAIR_END = tune_params[BISHOP_PAIR_END].current;
   Params::BISHOP_PAWN_PLACEMENT_END = tune_params[BISHOP_PAWN_PLACEMENT_END].current;
   Params::BAD_BISHOP_MID = tune_params[BAD_BISHOP_MID].current;
   Params::BAD_BISHOP_END = tune_params[BAD_BISHOP_END].current;
   Params::CENTER_PAWN_BLOCK = tune_params[CENTER_PAWN_BLOCK].current;
   Params::OUTSIDE_PASSER_MID = tune_params[OUTSIDE_PASSER_MID].current;
   Params::OUTSIDE_PASSER_END = tune_params[OUTSIDE_PASSER_END].current;
   Params::WEAK_PAWN_MID = tune_params[WEAK_PAWN_MID].current;
   Params::WEAK_PAWN_END = tune_params[WEAK_PAWN_END].current;
   Params::WEAK_ON_OPEN_FILE_MID = tune_params[WEAK_ON_OPEN_FILE_MID].current;
   Params::WEAK_ON_OPEN_FILE_END = tune_params[WEAK_ON_OPEN_FILE_END].current;
   Params::SPACE = tune_params[SPACE].current;
   Params::PAWN_CENTER_SCORE_MID = tune_params[PAWN_CENTER_SCORE_MID].current;
   Params::ROOK_ON_7TH_MID = tune_params[ROOK_ON_7TH_MID].current;
   Params::ROOK_ON_7TH_END = tune_params[ROOK_ON_7TH_END].current;
   Params::TWO_ROOKS_ON_7TH_MID = tune_params[TWO_ROOKS_ON_7TH_MID].current;
   Params::TWO_ROOKS_ON_7TH_END = tune_params[TWO_ROOKS_ON_7TH_END].current;
   Params::ROOK_ON_OPEN_FILE_MID = tune_params[ROOK_ON_OPEN_FILE_MID].current;
   Params::ROOK_ON_OPEN_FILE_END = tune_params[ROOK_ON_OPEN_FILE_END].current;
   Params::ROOK_BEHIND_PP_MID = tune_params[ROOK_BEHIND_PP_MID].current;
   Params::ROOK_BEHIND_PP_END = tune_params[ROOK_BEHIND_PP_END].current;
   Params::QUEEN_OUT = tune_params[QUEEN_OUT].current;
   Params::PAWN_SIDE_BONUS = tune_params[PAWN_SIDE_BONUS].current;
   Params::KING_OWN_PAWN_DISTANCE = tune_params[KING_OWN_PAWN_DISTANCE].current;
   Params::KING_OPP_PAWN_DISTANCE = tune_params[KING_OPP_PAWN_DISTANCE].current;
   Params::QUEENING_SQUARE_CONTROL_MID = tune_params[QUEENING_SQUARE_CONTROL_MID].current;
   Params::QUEENING_SQUARE_CONTROL_END = tune_params[QUEENING_SQUARE_CONTROL_END].current;
   Params::QUEENING_SQUARE_OPP_CONTROL_MID = tune_params[QUEENING_SQUARE_OPP_CONTROL_MID].current;
   Params::QUEENING_SQUARE_OPP_CONTROL_END = tune_params[QUEENING_SQUARE_OPP_CONTROL_END].current;
   Params::WRONG_COLOR_BISHOP = tune_params[WRONG_COLOR_BISHOP].current;
   Params::SIDE_PROTECTED_PAWN = tune_params[SIDE_PROTECTED_PAWN].current;
   for (int i = 0; i < 6; i++) {
      Params::KING_OPP_PASSER_DISTANCE[i] = PARAM(KING_OPP_PASSER_DISTANCE+i);
   }
   for (int i = 0; i < 3; i++) {
      Params::KING_POSITION_LOW_MATERIAL[i] = PARAM(KING_POSITION_LOW_MATERIAL0+i);
   }
   for (int i = 0; i < 8; i++) {
      Params::TRADE_DOWN[i] = PARAM(TRADE_DOWN+i);
   }
   for (int i = 0; i < 6; i++) {
      Params::RB_ADJUST[i] = PARAM(RB_ADJUST+i);
   }
   for (int i = 0; i < 6; i++) {
      Params::RBN_ADJUST[i] = PARAM(RBN_ADJUST+i);
   }
   for (int i = 0; i < 5; i++) {
      Params::QR_ADJUST[i] = PARAM(QR_ADJUST+i);
   }
   for (int i = 0; i < Params::KING_ATTACK_SCALE_SIZE; i++) {
       int x = int(PARAM(KING_ATTACK_SCALE_BIAS) +
           std::round(PARAM(KING_ATTACK_SCALE_MAX)/(1+exp(-PARAM(KING_ATTACK_SCALE_FACTOR)*(i-PARAM(KING_ATTACK_SCALE_INFLECT))/1000.0))));
       Params::KING_ATTACK_SCALE[i] = x;
   }
   memset(Params::PASSED_PAWN[0],'\0',sizeof(score_t)*8);
   memset(Params::PASSED_PAWN[1],'\0',sizeof(score_t)*8);
   for (int i = 2; i < 8; i++) {
      Params::PASSED_PAWN[Scoring::Midgame][i] = PARAM(PASSED_PAWN_MID2+i-2);
      Params::PASSED_PAWN[Scoring::Endgame][i] = PARAM(PASSED_PAWN_END2+i-2);
   }
   for (int i = 0; i < 4; i++) {
      Params::PASSED_PAWN_FILE_ADJUST[i] =
      Params::PASSED_PAWN_FILE_ADJUST[7-i] = PARAM(PASSED_PAWN_FILE_ADJUST1+i);
   }
   memset(Params::POTENTIAL_PASSER[0],'\0',sizeof(score_t)*8);
   memset(Params::POTENTIAL_PASSER[1],'\0',sizeof(score_t)*8);
   for (int i = 2; i < 7; i++) {
      Params::POTENTIAL_PASSER[Scoring::Midgame][i] = PARAM(POTENTIAL_PASSER_MID2+i-2);
      Params::POTENTIAL_PASSER[Scoring::Endgame][i] = PARAM(POTENTIAL_PASSER_END2+i-2);
   }
   memset(Params::CONNECTED_PASSER[0],'\0',sizeof(score_t)*8);
   memset(Params::CONNECTED_PASSER[1],'\0',sizeof(score_t)*8);
   for (int i = 2; i < 8; i++) {
      Params::CONNECTED_PASSER[Scoring::Midgame][i] = PARAM(CONNECTED_PASSER_MID2+i-2);
      Params::CONNECTED_PASSER[Scoring::Endgame][i] = PARAM(CONNECTED_PASSER_END2+i-2);
   }
   memset(Params::ADJACENT_PASSER[0],'\0',sizeof(score_t)*8);
   memset(Params::ADJACENT_PASSER[1],'\0',sizeof(score_t)*8);
   for (int i = 2; i < 8; i++) {
      Params::ADJACENT_PASSER[Scoring::Midgame][i] = PARAM(ADJACENT_PASSER_MID2+i-2);
      Params::ADJACENT_PASSER[Scoring::Endgame][i] = PARAM(ADJACENT_PASSER_END2+i-2);
   }
   memset(Params::DOUBLED_PAWNS[0],'\0',sizeof(score_t)*8);
   memset(Params::DOUBLED_PAWNS[1],'\0',sizeof(score_t)*8);
   memset(Params::TRIPLED_PAWNS[0],'\0',sizeof(score_t)*8);
   memset(Params::TRIPLED_PAWNS[1],'\0',sizeof(score_t)*8);
   memset(Params::ISOLATED_PAWN[0],'\0',sizeof(score_t)*8);
   memset(Params::ISOLATED_PAWN[1],'\0',sizeof(score_t)*8);
   for (int i = 0; i < 4; i++) {
      Params::DOUBLED_PAWNS[Scoring::Midgame][i] =
         Params::DOUBLED_PAWNS[Scoring::Midgame][7-i] =
         PARAM(DOUBLED_PAWNS_MID1+i);
      Params::DOUBLED_PAWNS[Scoring::Endgame][i] =
         Params::DOUBLED_PAWNS[Scoring::Endgame][7-i] =
         PARAM(DOUBLED_PAWNS_END1+i);
      Params::TRIPLED_PAWNS[Scoring::Midgame][i] =
         Params::TRIPLED_PAWNS[Scoring::Midgame][7-i] =
         PARAM(TRIPLED_PAWNS_MID1+i);
      Params::TRIPLED_PAWNS[Scoring::Endgame][i] =
         Params::TRIPLED_PAWNS[Scoring::Endgame][7-i] =
         PARAM(TRIPLED_PAWNS_END1+i);
      Params::ISOLATED_PAWN[Scoring::Midgame][i] =
         Params::ISOLATED_PAWN[Scoring::Midgame][7-i] =
         PARAM(ISOLATED_PAWN_MID1+i);
      Params::ISOLATED_PAWN[Scoring::Endgame][i] =
         Params::ISOLATED_PAWN[Scoring::Endgame][7-i] =
         PARAM(ISOLATED_PAWN_END1+i);
   }
   for (int p = 0; p < 2; p++) {
      for (int i = 0; i < 21; i++) {
         Params::PP_OWN_PIECE_BLOCK[p][i] =
            p == 0 ?
            PARAM(PP_OWN_PIECE_BLOCK_MID+i) :
            PARAM(PP_OWN_PIECE_BLOCK_END+i);
         Params::PP_OPP_PIECE_BLOCK[p][i] =
            p == 0 ?
            PARAM(PP_OPP_PIECE_BLOCK_MID+i) :
            PARAM(PP_OPP_PIECE_BLOCK_END+i);
      }
   }

   for (int i = 0; i < 9; i++) {
      Params::KNIGHT_MOBILITY[i] = PARAM(KNIGHT_MOBILITY+i);
   }
   for (int i = 0; i < 15; i++) {
      Params::BISHOP_MOBILITY[i] = PARAM(BISHOP_MOBILITY+i);
   }
   for (int i = 0; i < 15; i++) {
      Params::ROOK_MOBILITY[0][i] = PARAM(ROOK_MOBILITY_MIDGAME+i);
      Params::ROOK_MOBILITY[1][i] = PARAM(ROOK_MOBILITY_ENDGAME+i);
   }
   for (int i = 0; i < 24; i++) {
      Params::QUEEN_MOBILITY[0][i] = PARAM(QUEEN_MOBILITY_MIDGAME+i);
      Params::QUEEN_MOBILITY[1][i] = PARAM(QUEEN_MOBILITY_ENDGAME+i);
   }
   for (int i = 0; i < 5; i++) {
      Params::KING_MOBILITY_ENDGAME[i] = PARAM(KING_MOBILITY_ENDGAME+i);
   }

   for (int i = 0; i < 32; i++) {
      apply_to_pst(i,PARAM(KNIGHT_PST_MIDGAME+i),Params::KNIGHT_PST[0]);
      apply_to_pst(i,PARAM(KNIGHT_PST_ENDGAME+i),Params::KNIGHT_PST[1]);
      apply_to_pst(i,PARAM(BISHOP_PST_MIDGAME+i),Params::BISHOP_PST[0]);
      apply_to_pst(i,PARAM(BISHOP_PST_ENDGAME+i),Params::BISHOP_PST[1]);
      apply_to_pst(i,PARAM(ROOK_PST_MIDGAME+i),Params::ROOK_PST[0]);
      apply_to_pst(i,PARAM(ROOK_PST_ENDGAME+i),Params::ROOK_PST[1]);
      apply_to_pst(i,PARAM(QUEEN_PST_MIDGAME+i),Params::QUEEN_PST[0]);
      apply_to_pst(i,PARAM(QUEEN_PST_ENDGAME+i),Params::QUEEN_PST[1]);
      apply_to_pst(i,PARAM(KING_PST_MIDGAME+i),Params::KING_PST[0]);
      apply_to_pst(i,PARAM(KING_PST_ENDGAME+i),Params::KING_PST[1]);
   }
   int index = 0;
   for (int p = 0; p < 2; p++) {
      for (int i = 0; i < 16; i++) {
          for (int stage = 0; stage < 2; stage++,index++) {
              apply_to_outpost(i,p,stage,PARAM(KNIGHT_OUTPOST+index),Params::KNIGHT_OUTPOST);
          }
      }
   }
   index = 0;
   for (int p = 0; p < 2; p++) {
      for (int i = 0; i < 16; i++) {
          for (int stage = 0; stage < 2; stage++, index++) {
              apply_to_outpost(i,p,stage,PARAM(BISHOP_OUTPOST+index),Params::BISHOP_OUTPOST);
          }
      }
   }

}

void Tune::writeX0(ostream &o)
{
   for (int i=0; i < numTuningParams(); i++) {
      TuneParam p;
      getParam(i,p);
      o << p.name << ' ' << p.current << endl;
   }
   o << endl;
}

void Tune::readX0(istream &is)
{
   while (is.good()) {
      string in;
      getline(is,in);
      size_t pos = in.find(' ');
      if (pos == string::npos) continue;
      const string name = in.substr(0,pos);
      auto it = std::find_if(tune_params.begin(),tune_params.end(),[&name] (const TuneParam &p) { return p.name == name; });
      if (it == tune_params.end()) {
         cerr << "invalid param name found in input file: " << name << endl;
      } else {
         stringstream valstream(in.substr(pos+1,in.size()));
         score_t val;
         valstream >> val;
         if (!valstream.bad() && !valstream.fail()) {
            it->current = val;
         } else {
            cerr << "error parsing value for parameter " << name << endl;
         }
      }
   }
}

void Tune::getParam(int index, TuneParam &param) const
{
   param = tune_params[index];
}

score_t Tune::getParamValue(int index) const
{
   return tune_params[index].current;
}

void Tune::updateParamValue(int index, score_t value)
{
   tune_params[index].current = value;
}

// return index for parameter given name, -1 if not not found
int Tune::findParamByName(const string &name) const {
   int i = 0;
   for (vector<TuneParam>::const_iterator it = tune_params.begin();
        it != tune_params.end();
        it++,i++) {
      if ((*it).name == name) {
         return i;
      }
   }
   return -1;
}

double Tune::scale(score_t value,int index,int materialLevel) const
{
   ASSERT(materialLevel >= 0 && materialLevel<32);
   switch (tune_params[index].scaling) {
   case Tune::TuneParam::Any:
      return value;
   case Tune::TuneParam::Midgame:
      return (value*Params::MATERIAL_SCALE[materialLevel])/128.0;
   case Tune::TuneParam::Endgame:
      return (value*(128-Params::MATERIAL_SCALE[materialLevel]))/128.0;
   case Tune::TuneParam::None:
      return 0;
   }
   return 0;
}

score_t Tune::kingAttackSigmoid(score_t weight) const
{
    return PARAM(KING_ATTACK_SCALE_BIAS) +
        PARAM(KING_ATTACK_SCALE_MAX)/(1+exp(-PARAM(KING_ATTACK_SCALE_FACTOR)*(weight-PARAM(KING_ATTACK_SCALE_INFLECT))/1000.0));
}

