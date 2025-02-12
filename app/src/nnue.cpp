#include <algorithm>
#include <array>
#include <cstdint>

#include "nnue.h"
#include "weights.cpp"

struct Network {
	Accumulator feature_weights[2 * 6 * 64];
	Accumulator feature_bias;
	Accumulator output_weights[2];
	std::int16_t output_bias;

	int evaluate(const Accumulator& us, const Accumulator& them) const {
		static_assert(sizeof(Network) == 197440);

		int output = 0;

		// side to move
		for (int i = 0; i < HIDDEN_SIZE; i++) {
			std::int16_t input = std::clamp(us.vals[i], std::int16_t{0}, QA);
			std::int16_t weight = input * this->output_weights[0].vals[i];
			output += int{input} * int{weight};
		}

		// not side to move
		for (int i = 0; i < HIDDEN_SIZE; i++) {
			std::int16_t input = std::clamp(them.vals[i], std::int16_t{0}, QA);
			std::int16_t weight = input * this->output_weights[1].vals[i];
			output += int{input} * int{weight};
		}

		output /= int{QA};
		output += this->output_bias;
		output *= SCALE;
		output /= int{QA} * int{QB};

		return output;
	}

	void add_feature(Accumulator& acc, const int feature_idx) const {
		for (int i = 0; i < HIDDEN_SIZE; i++) {
			acc.vals[i] += this->feature_weights[feature_idx].vals[i];
		}
	}

	void remove_feature(Accumulator& acc, const int feature_idx) const {
		for (int i = 0; i < HIDDEN_SIZE; i++) {
			acc.vals[i] -= this->feature_weights[feature_idx].vals[i];
		}
	}
};

const Network *const NNUE = reinterpret_cast<const Network *>(weights_data);

Eval::Eval() : white{NNUE->feature_bias}, black{NNUE->feature_bias}
{
}

int Eval::evaluate(bool white_to_move) const
{
	if (white_to_move) {
		return NNUE->evaluate(this->white, this->black);
	}
	return NNUE->evaluate(this->black, this->white);
}

void Eval::add_piece(const int piece, const int square, const bool is_white)
{
	if (is_white) {
		NNUE->add_feature(this->white, 64 * piece + square);
		NNUE->add_feature(this->black, 64 * (6 + piece) + (square ^ 56));
	} else {
		NNUE->add_feature(this->black, 64 * piece + (square ^ 56));
		NNUE->add_feature(this->white, 64 * (6 + piece) + square);
	}
}

void Eval::remove_piece(const int piece, const int square, const bool is_white)
{
	if (is_white) {
		NNUE->remove_feature(this->white, 64 * piece + square);
		NNUE->remove_feature(this->black, 64 * (6 + piece) + (square ^ 56));
	} else {
		NNUE->remove_feature(this->black, 64 * piece + (square ^ 56));
		NNUE->remove_feature(this->white, 64 * (6 + piece) + square);
	}
}
