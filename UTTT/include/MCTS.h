#ifndef MCTS_H
#define MCTS_H

#include "Plateau.h"

class MCTS {
public:
    static GameMove getBestMove(const Plateau& currentState, int maxIterations, int myPlayer, int maxMillis = 1000);
};

#endif // MCTS_H
