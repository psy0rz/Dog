#include <libchess/Position.h>
#include "nnue.h"


int nnue_evaluate(const libchess::Position & pos)
{
	Eval e;
        for(libchess::PieceType type : libchess::constants::PIECE_TYPES) {
                libchess::Bitboard piece_bb_w = pos.piece_type_bb(type, libchess::constants::WHITE);
                while (piece_bb_w) {
                        libchess::Square sq = piece_bb_w.forward_bitscan();
                        piece_bb_w.forward_popbit();
			e.add_piece(type, sq, true);
                }

                libchess::Bitboard piece_bb_b = pos.piece_type_bb(type, libchess::constants::BLACK);
                while (piece_bb_b) {
                        libchess::Square sq = piece_bb_b.forward_bitscan();
                        piece_bb_b.forward_popbit();
			e.add_piece(type, sq, false);
                }
        }

        return e.evaluate(pos.side_to_move() == libchess::constants::WHITE);
}
