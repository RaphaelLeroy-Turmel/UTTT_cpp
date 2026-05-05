#include "MCTS.h"
#include <cmath>
#include <cstring>
#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>

// =====================================================================
// RESUME DES AMELIORATIONS PAR RAPPORT A LA VERSION PRECEDENTE
// =====================================================================
// [1] UCT_C           : 0.8 => 1.2   (meilleur équilibre exploration/exploitation)
// [2] SIM_DEPTH       : 12   => 40    (simulations bien plus représentatives)
// [3] Free Move pénalité : 300 => 120 (n'écrase plus le score d'une macro gagnée)
// [4] getSmartRolloutMove : sans copies de FastPlateau (bitboards directs)
//       => ajout wouldWinMacro() et wouldWinGame() dans FastPlateau
// [5] Expansion : détection coup gagnant/bloquant AVANT la sélection aléatoire
// [6] Move ordering dans allocNode : centre > coins > bords explorés en premier
// [7] Terminal caching : cachedWinner évite de re-simuler depuis un état connu
// [8] Pondération hybride par profondeur : plus on simule profond, plus on
//       fait confiance au rollout vs. l'évaluation immédiate
// [9] Heuristique : ajout d'un bonus "deux-en-ligne" (menaces)
// =====================================================================
// NOTE RAVE/AMAF : non implémenté car requiert ~81 * 8 octets supplémentaires
// par nœud => +800 Mo pour 1.2M nœuds. À envisager avec un pool réduit.
// =====================================================================


// =====================================================================
// 1. GENERATEUR ALEATOIRE ULTRA-RAPIDE (inchangé)
// =====================================================================
static uint32_t xor_state = 123456789;
inline uint32_t fast_rand() {
    xor_state ^= xor_state << 13;
    xor_state ^= xor_state >> 17;
    xor_state ^= xor_state << 5;
    return xor_state;
}

static const uint16_t WIN_PATTERNS[8] = {
    0b000000111, 0b000111000, 0b111000000,
    0b001001001, 0b010010010, 0b100100100,
    0b100010001, 0b001010100
};

inline bool checkWin16(uint16_t board) {
    for (int i = 0; i < 8; i++) {
        if ((board & WIN_PATTERNS[i]) == WIN_PATTERNS[i]) return true;
    }
    return false;
}


// =====================================================================
// 2. LE FAST-PLATEAU (Bitboards) — ajout de wouldWinMacro/Game
// =====================================================================
struct FastPlateau {
    uint16_t p[2][9];
    uint16_t macro[2];
    uint16_t macroDraw;
    int target;

    void initFromPlateau(const Plateau& origin) {
        memset(this, 0, sizeof(FastPlateau));
        for (int r = 0; r < 9; r++) {
            for (int c = 0; c < 9; c++) {
                int m    = (r / 3) * 3 + (c / 3);
                int cell = (r % 3) * 3 + (c % 3);
                if      (origin.grille[r][c] == 1) p[0][m] |= (1 << cell);
                else if (origin.grille[r][c] == 2) p[1][m] |= (1 << cell);
            }
        }
        for (int mR = 0; mR < 3; ++mR) {
            for (int mC = 0; mC < 3; ++mC) {
                int m   = mR * 3 + mC;
                int val = origin.macroGrilles[mR][mC];
                if      (val == 1) macro[0]    |= (1 << m);
                else if (val == 2) macro[1]    |= (1 << m);
                else if (val == 3) macroDraw   |= (1 << m);
            }
        }
        target = (origin.cibleLigne == -1) ? -1 : (origin.cibleLigne * 3 + origin.cibleColonne);
    }

    inline int getMoves(uint8_t* movesOut) const {
        int count = 0;
        if (target != -1) {
            uint16_t occupied = p[0][target] | p[1][target];
            for (int c = 0; c < 9; c++)
                if (!(occupied & (1 << c))) movesOut[count++] = target * 9 + c;
        } else {
            for (int m = 0; m < 9; m++) {
                if (!(macro[0] & (1 << m)) && !(macro[1] & (1 << m)) && !(macroDraw & (1 << m))) {
                    uint16_t occupied = p[0][m] | p[1][m];
                    for (int c = 0; c < 9; c++)
                        if (!(occupied & (1 << c))) movesOut[count++] = m * 9 + c;
                }
            }
        }
        return count;
    }

    inline void playMove(uint8_t move, int player) {
        int m   = move / 9;
        int c   = move % 9;
        int pId = player - 1;

        p[pId][m] |= (1 << c);

        if (checkWin16(p[pId][m])) {
            macro[pId] |= (1 << m);
        } else if (((p[0][m] | p[1][m]) & 0x1FF) == 0x1FF) {
            macroDraw |= (1 << m);
        }

        target = c;
        if ((macro[0] & (1 << target)) || (macro[1] & (1 << target)) || (macroDraw & (1 << target))) {
            target = -1;
        }
    }

    inline int getWinner() const {
        if (checkWin16(macro[0])) return 1;
        if (checkWin16(macro[1])) return 2;
        if (((macro[0] | macro[1] | macroDraw) & 0x1FF) == 0x1FF) return 3;
        return 0;
    }

    // ----------------------------------------------------------------
    // AMELIORATION [4] : vérifications rapides sans copie du plateau
    // ----------------------------------------------------------------

    // Vrai si jouer (m,c) pour pId gagne la mini-grille m
    inline bool wouldWinMacro(int m, int c, int pId) const {
        uint16_t newBoard = p[pId][m] | (1 << c);
        return checkWin16(newBoard);
    }

    // Vrai si jouer (m,c) pour pId gagne la partie entière
    inline bool wouldWinGame(int m, int c, int pId) const {
        if (!wouldWinMacro(m, c, pId)) return false;
        uint16_t newMacro = macro[pId] | (1 << m);
        return checkWin16(newMacro);
    }
};


// =====================================================================
// 3. MEMORY POOL ET TREE REUSE — ajout du cachedWinner
// =====================================================================
struct Node {
    int     parentIdx;
    int     firstChildIdx;
    int     nextSiblingIdx;
    uint8_t move;
    uint8_t numUntried;
    uint8_t untriedMoves[81];
    int     playerJustMoved;
    int     visits;
    double  wins;
    int8_t  cachedWinner; // AMELIORATION [7] : -1=inconnu, 0=pas terminal, 1/2/3=résultat
};

const int MAX_NODES = 1200000;
static Node nodePool[MAX_NODES];
static int  nodeCount = 0;

static int         previousRootIdx = -1;
static FastPlateau previousRootState;

// ----------------------------------------------------------------
// AMELIORATION [6] : allocNode avec tri centre > coins > bords
// Les coups prioritaires sont mis EN FIN de tableau car on les
// pioche depuis la fin à l'expansion (évite un swap coûteux).
// ----------------------------------------------------------------
inline int allocNode(int parent, uint8_t move, const FastPlateau& state, int playerJM) {
    if (nodeCount >= MAX_NODES) return -1;
    int  idx = nodeCount++;
    Node& n  = nodePool[idx];

    n.parentIdx      = parent;
    n.firstChildIdx  = -1;
    n.nextSiblingIdx = -1;
    n.move           = move;
    n.playerJustMoved = playerJM;
    n.visits         = 0;
    n.wins           = 0.0;
    n.cachedWinner   = -1;

    // Récupération des coups bruts
    uint8_t rawMoves[81];
    int rawCount = state.getMoves(rawMoves);

    // Tri : bords d'abord (explorés en dernier), puis coins, puis centre
    // → Centre et coins se retrouvent en fin de tableau et sont joués en priorité
    uint8_t borderMoves[81], cornerMoves[81], centerMoves[81];
    int bCnt = 0, coCnt = 0, ceCnt = 0;

    for (int i = 0; i < rawCount; i++) {
        int c = rawMoves[i] % 9; // cellule dans la mini-grille
        if      (c == 4)                           centerMoves[ceCnt++] = rawMoves[i];
        else if (c == 0 || c == 2 || c == 6 || c == 8) cornerMoves[coCnt++] = rawMoves[i];
        else                                        borderMoves[bCnt++]  = rawMoves[i];
    }

    int count = 0;
    for (int i = 0; i < bCnt;  i++) n.untriedMoves[count++] = borderMoves[i];
    for (int i = 0; i < coCnt; i++) n.untriedMoves[count++] = cornerMoves[i];
    for (int i = 0; i < ceCnt; i++) n.untriedMoves[count++] = centerMoves[i];
    n.numUntried = (uint8_t)count;

    return idx;
}


// =====================================================================
// 4. EVALUATION HEURISTIQUE — pénalité free move recalibrée + menaces
// =====================================================================
static double evaluateBoard(const FastPlateau& state, int myPlayer, int playerToMove) {
    int    oppPlayer = (myPlayer == 1) ? 2 : 1;
    double scoreDiff = 0.0;

    const double WEIGHT_MACRO_WON  = 500.0;
    const double BONUS_CENTER      =  50.0;
    const double BONUS_CORNER      =  20.0;
    const double WEIGHT_CELL       =   2.0;
    const double BONUS_TWO_IN_ROW  =  15.0; // AMELIORATION [9]

    for (int m = 0; m < 9; ++m) {
        double posBonus = (m == 4) ? BONUS_CENTER : ((m % 2 == 0) ? BONUS_CORNER : 0.0);

        if (state.macro[myPlayer - 1] & (1 << m)) {
            scoreDiff += (WEIGHT_MACRO_WON + posBonus);
        } else if (state.macro[oppPlayer - 1] & (1 << m)) {
            scoreDiff -= (WEIGHT_MACRO_WON + posBonus);
        } else {
            // Score cellules dans la mini-grille
            int myBits  = __builtin_popcount(state.p[myPlayer  - 1][m]);
            int oppBits = __builtin_popcount(state.p[oppPlayer - 1][m]);
            scoreDiff += (myBits - oppBits) * WEIGHT_CELL;

            // AMELIORATION [9] : bonus/malus pour les "deux-en-ligne" (menaces)
            for (int i = 0; i < 8; i++) {
                uint16_t pat      = WIN_PATTERNS[i];
                uint16_t myInPat  = state.p[myPlayer  - 1][m] & pat;
                uint16_t oppInPat = state.p[oppPlayer - 1][m] & pat;

                // 2 pièces de ma couleur dans ce pattern, aucune adverse → menace
                if (__builtin_popcount(myInPat)  == 2 && oppInPat == 0) scoreDiff += BONUS_TWO_IN_ROW;
                if (__builtin_popcount(oppInPat) == 2 && myInPat  == 0) scoreDiff -= BONUS_TWO_IN_ROW;
            }
        }
    }

    // AMELIORATION [3] : Free Move pénalité recalibrée 300 → 120
    // (un coup libre ne vaut pas 60% d'une macro-case gagnée)
    if (state.target == -1) {
        if (playerToMove == myPlayer) scoreDiff += 120.0;
        else                          scoreDiff -= 120.0;
    }

    return 1.0 / (1.0 + std::exp(-scoreDiff / 30.0));
}


// =====================================================================
// 5. SMART ROLLOUT — sans copies de plateau (AMELIORATION [4])
// =====================================================================
inline int getSmartRolloutMove(const FastPlateau& state, int currentPlayer,
                               const uint8_t* moves, int numMoves)
{
    int pId    = currentPlayer - 1;
    int oppPId = 1 - pId;

    int backupWinMacro  = -1; // coup qui gagne une mini-grille (mais pas la partie)
    int backupBlockGame = -1; // coup qui bloque une victoire de partie adverse
    int backupBlockMacro= -1; // coup qui bloque une victoire de mini-grille adverse

    for (int i = 0; i < numMoves; ++i) {
        int m = moves[i] / 9;
        int c = moves[i] % 9;

        // Priorité 1 : gagner la PARTIE immédiatement
        if (state.wouldWinGame(m, c, pId)) return i;

        // Priorité 2 : gagner une mini-grille (mémorisé)
        if (backupWinMacro == -1 && state.wouldWinMacro(m, c, pId))
            backupWinMacro = i;
    }
    if (backupWinMacro != -1) return backupWinMacro;

    // Priorité 3 : bloquer une victoire de PARTIE adverse
    for (int i = 0; i < numMoves; ++i) {
        int m = moves[i] / 9;
        int c = moves[i] % 9;

        if (state.wouldWinGame(m, c, oppPId)) return i;

        // Priorité 4 : bloquer une victoire de mini-grille adverse (mémorisé)
        if (backupBlockMacro == -1 && state.wouldWinMacro(m, c, oppPId))
            backupBlockMacro = i;
    }
    if (backupBlockMacro != -1) return backupBlockMacro;

    return fast_rand() % numMoves;
}


// =====================================================================
// 6. CONSTANTES MCTS TUNEES
// =====================================================================
static const double UCT_C     = 1.2; // AMELIORATION [1] : était 0.8
static const int    SIM_DEPTH = 40;  // AMELIORATION [2] : était 12


// =====================================================================
// 7. LE MOTEUR MCTS PRINCIPAL
// =====================================================================
GameMove MCTS::getBestMove(const Plateau& currentState, int maxIterations, int myPlayer, int maxMillis)
{
    auto startTime = std::chrono::steady_clock::now();
    xor_state = (uint32_t)std::chrono::system_clock::now().time_since_epoch().count() | 1;

    FastPlateau currentFastState;
    currentFastState.initFromPlateau(currentState);
    int opponent = (myPlayer == 1) ? 2 : 1;

    // ------------------------------------------------------------------
    // LOG DES 30 PREMIERS COUPS (inchangé)
    // ------------------------------------------------------------------
    static std::vector<std::pair<int, double>> evalHistory;
    static bool logPrinted = false;

    int piecesCount = 0;
    for (int i = 0; i < 9; ++i)
        piecesCount += __builtin_popcount(currentFastState.p[0][i])
                     + __builtin_popcount(currentFastState.p[1][i]);
    int turnNumber = piecesCount + 1;

    if (turnNumber <= 2) { evalHistory.clear(); logPrinted = false; }

    if (!logPrinted && turnNumber <= 30) {
        double currentEval = evaluateBoard(currentFastState, myPlayer, myPlayer);
        evalHistory.push_back({turnNumber, currentEval});
        if (turnNumber >= 29) {
            std::cout << "evaluation pour les 30 premier coups : ";
            for (size_t i = 0; i < evalHistory.size(); ++i) {
                std::cout << evalHistory[i].first << "-"
                          << std::round(evalHistory[i].second * 100.0) / 100.0;
                if (i < evalHistory.size() - 1) std::cout << ", ";
            }
            std::cout << std::endl;
            logPrinted = true;
        }
    }

    // ------------------------------------------------------------------
    // TREE REUSE (inchangé dans sa logique, compatibilité cachedWinner)
    // ------------------------------------------------------------------
    int rootIdx = -1;

    if (previousRootIdx != -1 && nodeCount < 800000) {
        int childIdx = nodePool[previousRootIdx].firstChildIdx;
        while (childIdx != -1) {
            FastPlateau childState = previousRootState;
            childState.playMove(nodePool[childIdx].move, opponent);
            if (memcmp(&childState, &currentFastState, sizeof(FastPlateau)) == 0) {
                rootIdx = childIdx;
                break;
            }
            childIdx = nodePool[childIdx].nextSiblingIdx;
        }
    }

    if (rootIdx == -1) {
        nodeCount = 0;
        rootIdx = allocNode(-1, 0, currentFastState, opponent);
    }

    // ------------------------------------------------------------------
    // BOUCLE MCTS PRINCIPALE
    // ------------------------------------------------------------------
    for (int iter = 0; iter < maxIterations; ++iter) {

        // Vérification du temps toutes les 500 itérations
        if (iter % 500 == 0) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count() > maxMillis)
                break;
        }

        int currIdx = rootIdx;
        FastPlateau state = currentFastState;

        // ==============================================================
        // PHASE 1 : SELECTION (UCT recalibré + court-circuit terminal)
        // ==============================================================
        while (nodePool[currIdx].numUntried == 0 && nodePool[currIdx].firstChildIdx != -1) {

            // AMELIORATION [7] : court-circuit si ce nœud est déjà terminal
            if (nodePool[currIdx].cachedWinner > 0) break;

            int    bestChild = -1;
            double bestUCT   = -1e9;
            double logVisits = std::log((double)nodePool[currIdx].visits + 1.0);
            int    childIdx  = nodePool[currIdx].firstChildIdx;

            while (childIdx != -1) {
                Node& child = nodePool[childIdx];
                double uct;

                if (child.visits == 0) {
                    // Nœud jamais visité : valeur optimiste pour forcer l'exploration
                    uct = 1.0 + UCT_C * std::sqrt(logVisits);
                } else {
                    // AMELIORATION [1] : UCT_C = 1.2 au lieu de 0.8
                    uct = (child.wins / child.visits) +
                          UCT_C * std::sqrt(logVisits / (double)child.visits);
                }

                if (uct > bestUCT) { bestUCT = uct; bestChild = childIdx; }
                childIdx = child.nextSiblingIdx;
            }

            if (bestChild == -1) break;
            currIdx = bestChild;
            state.playMove(nodePool[currIdx].move, nodePool[currIdx].playerJustMoved);
        }

        // ==============================================================
        // PHASE 2 : EXPANSION avec détection de coup gagnant (AMELIORATION [5])
        // ==============================================================
        if (nodePool[currIdx].numUntried > 0 && state.getWinner() == 0) {

            int nextPlayer = (nodePool[currIdx].playerJustMoved == 1) ? 2 : 1;
            int nextPId    = nextPlayer - 1;
            int oppPId     = 1 - nextPId;

            // Chercher parmi les coups non testés un coup immédiatement gagnant
            // ou bloquant, en priorité absolue sur l'ordre de tri
            int winIdx   = -1;
            int blockIdx = -1;

            for (int i = 0; i < nodePool[currIdx].numUntried; i++) {
                uint8_t mv = nodePool[currIdx].untriedMoves[i];
                int m = mv / 9, c = mv % 9;

                if (state.wouldWinGame(m, c, nextPId)) { winIdx = i; break; }
                if (blockIdx == -1 && state.wouldWinGame(m, c, oppPId)) blockIdx = i;
            }

            int r;
            if      (winIdx   != -1) r = winIdx;    // coup gagnant → priorité absolue
            else if (blockIdx != -1) r = blockIdx;  // blocage → seconde priorité
            else                     r = nodePool[currIdx].numUntried - 1;
            // ↑ AMELIORATION [6] : on prend depuis la fin (centre/coins en priorité)

            uint8_t moveToPlay = nodePool[currIdx].untriedMoves[r];
            // Remplacement par le dernier élément pour éviter le décalage
            nodePool[currIdx].untriedMoves[r] = nodePool[currIdx].untriedMoves[--nodePool[currIdx].numUntried];

            state.playMove(moveToPlay, nextPlayer);

            int newChildIdx = allocNode(currIdx, moveToPlay, state, nextPlayer);
            if (newChildIdx == -1) break;

            nodePool[newChildIdx].nextSiblingIdx = nodePool[currIdx].firstChildIdx;
            nodePool[currIdx].firstChildIdx      = newChildIdx;
            currIdx = newChildIdx;
        }

        // ==============================================================
        // PHASE 3 : SIMULATION HYBRIDE (profondeur 40 + pondération depth)
        // ==============================================================
        int currentPlayer = (nodePool[currIdx].playerJustMoved == 1) ? 2 : 1;

        // AMELIORATION [7] : vérifier et mémoriser si on est déjà sur un état terminal
        int winner = state.getWinner();
        if (winner != 0 && nodePool[currIdx].cachedWinner == -1)
            nodePool[currIdx].cachedWinner = (int8_t)winner;

        // Évaluation immédiate AVANT la simulation (pour la pondération)
        double evalImmediate = evaluateBoard(state, myPlayer, currentPlayer);

        int     depth    = 0;
        uint8_t simMoves[81];

        // AMELIORATION [2] : SIM_DEPTH = 40 au lieu de 12
        while (winner == 0 && depth < SIM_DEPTH) {
            int numMoves = state.getMoves(simMoves);
            if (numMoves == 0) break;

            // AMELIORATION [4] : rollout sans copie de plateau
            int moveIdx = getSmartRolloutMove(state, currentPlayer, simMoves, numMoves);
            state.playMove(simMoves[moveIdx], currentPlayer);
            currentPlayer = (currentPlayer == 1) ? 2 : 1;
            winner        = state.getWinner();
            depth++;
        }

        // Calcul du score final
        double resultScore;
        if      (winner == myPlayer) resultScore = 1.0;
        else if (winner == opponent) resultScore = 0.0;
        else if (winner == 3)        resultScore = 0.5;
        else {
            // AMELIORATION [8] : pondération par profondeur atteinte
            // Plus la simulation est longue, plus on fait confiance au rollout
            double evalRollout  = evaluateBoard(state, myPlayer, currentPlayer);
            double depthWeight  = (double)depth / (double)SIM_DEPTH; // ∈ [0, 1]
            resultScore = evalImmediate * (1.0 - depthWeight)
                        + evalRollout   * depthWeight;
        }

        // ==============================================================
        // PHASE 4 : RETROPROPAGATION (inchangée dans sa logique)
        // ==============================================================
        while (currIdx != -1) {
            nodePool[currIdx].visits++;
            if (nodePool[currIdx].playerJustMoved == myPlayer)
                nodePool[currIdx].wins += resultScore;
            else
                nodePool[currIdx].wins += (1.0 - resultScore);

            if (currIdx == rootIdx) break;
            currIdx = nodePool[currIdx].parentIdx;
        }
    }

    // ------------------------------------------------------------------
    // DECISION FINALE : coup le plus visité
    // ------------------------------------------------------------------
    int bestChild = -1;
    int maxVisits = -1;
    int childIdx  = nodePool[rootIdx].firstChildIdx;

    while (childIdx != -1) {
        if (nodePool[childIdx].visits > maxVisits) {
            maxVisits = nodePool[childIdx].visits;
            bestChild = childIdx;
        }
        childIdx = nodePool[childIdx].nextSiblingIdx;
    }

    if (bestChild == -1) return {-1, -1};

    // Mise à jour du tree reuse
    previousRootIdx   = bestChild;
    previousRootState = currentFastState;
    previousRootState.playMove(nodePool[bestChild].move, myPlayer);

    uint8_t bestMoveLinear = nodePool[bestChild].move;
    int m = bestMoveLinear / 9;
    int c = bestMoveLinear % 9;
    return {(m / 3) * 3 + (c / 3), (m % 3) * 3 + (c % 3)};
}
