#include <iostream>
#include <ctime>
#include <cstdlib>
#include "main.h"
#include "Plateau.h"
#include <vector>
#include "MCTS.h"

// --- ALGORITHME 1 : RANDOM ---
GameMove algoRandom(const Plateau& p) {
    std::vector<GameMove> coups = p.getCoupsPossibles();

    // Si aucun coup n'est possible (fin de partie)
    if (coups.empty()) return {-1, -1};

    // Choix d'un index au hasard parmi les coups légaux
    int index = std::rand() % coups.size();
    return coups[index];
}
// -----------------------------




// Structure pour retourner le coup
struct Coord { int x, y; };

Coord trouverCoupGagnant(const Plateau& p, int mR, int mC, int joueur) {
    // Délimitation de la macro-case (ex: si mR=1, mC=0, on regarde les lignes 3,4,5 et col 0,1,2)
    int startR = mR * 3;
    int startC = mC * 3;
    int cible = 2*joueur;
    //si joueur 1 (l'adversaire) on va rchercehr les coups qui le font gagner immédiatement
    //donc ceux qui nous dfont perdre immédiatement au prochain tour
    //si joueur 2 (notre IA) on va chercher les coups qui nous font gagner la mini grille immédiatement


    for (int i = startR; i < startR + 3; i++) {
        for (int j = startC; j < startC + 3; j++) {

            // On définit les 4 directions : Horizontale, Verticale, Diagonale \, Diagonale /
            int dr[] = {0, 1, 1, 1};
            int dc[] = {1, 0, 1, -1};

            for (int d = 0; d < 4; d++) {
                int r1 = i + dr[d], c1 = j + dc[d];
                int r2 = i + 2 * dr[d], c2 = j + 2 * dc[d];

                // On vérifie qu'on ne sort pas de la petite grille 3x3
                if (r2 >= startR && r2 < startR + 3 && c2 >= startC && c2 < startC + 3) {

                    int v0 = p.grille[i][j];
                    int v1 = p.grille[r1][c1];
                    int v2 = p.grille[r2][c2];

                    // Somme = 4 signifie deux ronds (2+2) et une case vide (0)
                    if (v0 + v1 + v2 == 4) {
                        if (v0 == 0) return {i, j};
                        if (v1 == 0) return {r1, c1};
                        if (v2 == 0) return {r2, c2};
                    }
                }
            }
        }
    }
    return {-1, -1}; // Aucun coup gagnant trouvé
}

// Algo 2 : Glouton (Greedy)
GameMove algoGreedy(const Plateau& p) {
    std::vector<GameMove> coups = p.getCoupsPossibles();
    if (coups.empty()) return {-1, -1};

    // On extrait mR et mC à partir du premier coup possible trouvé
    int mR = coups[0].row / 3;
    int mC = coups[0].col / 3;

    Coord gagnant = trouverCoupGagnant(p, mR, mC,2);

    if (gagnant.x != -1) {
        return {gagnant.x, gagnant.y};
    } else {
        // Sinon, choix au hasard
        int index = std::rand() % coups.size();
        return coups[index];
    }
}

// Algo 3 : Glouton+ (version 1)
GameMove algoGreedyPlusV1(const Plateau& p) {
    std::vector<GameMove> coups = p.getCoupsPossibles();
    if (coups.empty()) return {-1, -1};

    // On extrait mR et mC à partir du premier coup possible trouvé
    int mR = coups[0].row / 3;
    int mC = coups[0].col / 3;

    Coord gagnant = trouverCoupGagnant(p, mR, mC,2);

    if (gagnant.x != -1) {
        return {gagnant.x, gagnant.y};
    } else {// si pas de coup gagnant immédiat alors on choisi le coup qui nous empeche de perdre immédiatement:
        Coord nonPerdant = trouverCoupGagnant(p, mR, mC,1); // on trouve le coup gagnant pour l'adversaire,
                                                      //on peut éviter de perdre au prochain tour s'il n'a qu'un seul coup gagnant immédiat
        if (nonPerdant.x != -1) {
            return {nonPerdant.x, nonPerdant.y};
        } else {
            // Sinon, choix au hasard
            int index = std::rand() % coups.size();
            return coups[index];
        }
    }
}


int main()
{
    // Initialisation de la graine aléatoire
    std::srand(std::time(nullptr));

    // Test : 100 parties, mode ARENA, niveau EASY_1
    game.initialize(100, Level::MEDIUM_2, Mode::ARENA, false, "Bot_MTS_500K_Feval_v4_depth40");

    int victoires = 0;
    int defaites = 0;
    int nuls = 0;

    while (!game.isAllGameFinish())
    {
        Plateau p; // On réinitialise notre plateau à chaque nouvelle partie
        GameMove myMove{-1, -1};

        while (!game.isFinish())
        {
            GameMove oppMove;
            game.getMove(oppMove);

            // 1. On enregistre le coup de l'adversaire
            if(oppMove.row != -1) {
                p.jouerCoup(oppMove, 1);
            }

            // 2. Notre algorithme choisit son coup
                //myMove = myMove = MCTS::getBestMove(p, 200000, 2, 1000);
            myMove = MCTS::getBestMove(p, 500000, 2, 800);

            // 3. On enregistre notre coup et on l'envoie à la DLL
            if (myMove.row != -1) {
                p.jouerCoup(myMove, 2);
                game.setMove(myMove);
            }
        }

        // Comptage des statistiques à la fin de chaque partie
        Winner w = game.getWinner();
        if (w == Winner::IA) victoires++; // Dans la DLL, tu es considéré comme l'IA
        else if (w == Winner::PLAYER) defaites++; // Le prof est considéré comme le joueur
        else nuls++;
    }

    // Affichage des statistiques finales
    std::cout << "--- RESULTATS ALGO RANDOM (100 parties) ---" << std::endl;
    std::cout << "Victoires : " << victoires << "%" << std::endl;
    std::cout << "Defaites  : " << defaites << "%" << std::endl;
    std::cout << "Nuls      : " << nuls << "%" << std::endl;

    return 0;
}
