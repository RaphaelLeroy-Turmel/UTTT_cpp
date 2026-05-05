#ifndef PLATEAU_H
#define PLATEAU_H
#include "../main.h" // Le .. permet de remonter du dossier 'include' vers 'Headers'
#include <vector>

class Plateau {
public:
    int grille[9][9];
    int macroGrilles[3][3];
    int cibleLigne;
    int cibleColonne;

    Plateau() {
        for(int i = 0; i < 9; ++i) for(int j = 0; j < 9; ++j) grille[i][j] = 0;
        for(int i = 0; i < 3; ++i) for(int j = 0; j < 3; ++j) macroGrilles[i][j] = 0;
        cibleLigne = -1; // -1 = libre de jouer partout
        cibleColonne = -1;
    }

    // Renvoie tous les coups légaux selon la règle de l'Ultimate Tic-Tac-Toe
    std::vector<GameMove> getCoupsPossibles() const {
        std::vector<GameMove> coups;
        for (int r = 0; r < 9; ++r) {
            for (int c = 0; c < 9; ++c) {
                if (grille[r][c] == 0) { // Si la case est vide
                    int macroR = r / 3;
                    int macroC = c / 3;

                    if (macroGrilles[macroR][macroC] == 0) { // Si la macro-grille n'est pas finie
                        if ((cibleLigne == -1 && cibleColonne == -1) ||
                            (cibleLigne == macroR && cibleColonne == macroC)) {
                            coups.push_back({r, c});
                        }
                    }
                }
            }
        }
        return coups;
    }

    void verifierFinMacro(int mR, int mC) {
    // Vérification des lignes dans la petite grille
    for (int i = 0; i < 3; i++) {
        if (grille[mR*3 + i][mC*3] != 0 &&
            grille[mR*3 + i][mC*3] == grille[mR*3 + i][mC*3 + 1] &&
            grille[mR*3 + i][mC*3 + 1] == grille[mR*3 + i][mC*3 + 2]) {
            macroGrilles[mR][mC] = grille[mR*3 + i][mC*3];
            return;
        }
    }
    // Vérification des colonnes
    for (int i = 0; i < 3; i++) {
        if (grille[mR*3][mC*3 + i] != 0 &&
            grille[mR*3][mC*3 + i] == grille[mR*3 + 1][mC*3 + i] &&
            grille[mR*3 + 1][mC*3 + i] == grille[mR*3 + 2][mC*3 + i]) {
            macroGrilles[mR][mC] = grille[mR*3][mC*3 + i];
            return;
        }
    }
    // Diagonales
    if (grille[mR*3][mC*3] != 0 && grille[mR*3][mC*3] == grille[mR*3+1][mC*3+1] && grille[mR*3+1][mC*3+1] == grille[mR*3+2][mC*3+2]) {
        macroGrilles[mR][mC] = grille[mR*3][mC*3]; return;
    }
    if (grille[mR*3][mC*3+2] != 0 && grille[mR*3][mC*3+2] == grille[mR*3+1][mC*3+1] && grille[mR*3+1][mC*3+1] == grille[mR*3+2][mC*3]) {
        macroGrilles[mR][mC] = grille[mR*3][mC*3+2]; return;
    }

    // Vérifier si la grille est pleine (Match Nul local)
    bool pleine = true;
    for(int i=0; i<3; i++) for(int j=0; j<3; j++) if(grille[mR*3+i][mC*3+j] == 0) pleine = false;
    if(pleine) macroGrilles[mR][mC] = 3;
}

    // Met à jour le plateau interne après un coup
    void jouerCoup(GameMove coup, int joueur) {
    if(coup.row < 0 || coup.row > 8) return;
    grille[coup.row][coup.col] = joueur;

    // On vérifie si la petite grille est finie
    verifierFinMacro(coup.row / 3, coup.col / 3);

    // Règle de projection
    cibleLigne = coup.row % 3;
    cibleColonne = coup.col % 3;

    // Si la cible est déjà finie, on libère le jeu (-1)
    if (macroGrilles[cibleLigne][cibleColonne] != 0) {
        cibleLigne = -1;
        cibleColonne = -1;
    }


}
    // Sauvegarde l'état pour annulation
struct EtatCoup {
    int cibleL, cibleC, macroOld;
};

EtatCoup jouerCoupVirtuel(GameMove coup, int joueur) {
    EtatCoup etat = {cibleLigne, cibleColonne, macroGrilles[coup.row/3][coup.col/3]};
    grille[coup.row][coup.col] = joueur;
    verifierFinMacro(coup.row / 3, coup.col / 3);

    cibleLigne = coup.row % 3;
    cibleColonne = coup.col % 3;
    if (macroGrilles[cibleLigne][cibleColonne] != 0) {
        cibleLigne = -1; cibleColonne = -1;
    }
    return etat;
}

void annulerCoupVirtuel(GameMove coup, EtatCoup etat) {
    grille[coup.row][coup.col] = 0;
    macroGrilles[coup.row/3][coup.col/3] = etat.macroOld;
    cibleLigne = etat.cibleL;
    cibleColonne = etat.cibleC;
}

// À ajouter dans la classe Plateau
int getWinner() const {
    // 1. Vérifier les lignes de la macro-grille
    for (int i = 0; i < 3; i++) {
        if (macroGrilles[i][0] != 0 && macroGrilles[i][0] != 3 &&
            macroGrilles[i][0] == macroGrilles[i][1] && macroGrilles[i][1] == macroGrilles[i][2])
            return macroGrilles[i][0];
    }
    // 2. Vérifier les colonnes de la macro-grille
    for (int i = 0; i < 3; i++) {
        if (macroGrilles[0][i] != 0 && macroGrilles[0][i] != 3 &&
            macroGrilles[0][i] == macroGrilles[1][i] && macroGrilles[1][i] == macroGrilles[2][i])
            return macroGrilles[0][i];
    }
    // 3. Diagonales
    if (macroGrilles[0][0] != 0 && macroGrilles[0][0] != 3 && macroGrilles[0][0] == macroGrilles[1][1] && macroGrilles[1][1] == macroGrilles[2][2])
        return macroGrilles[0][0];
    if (macroGrilles[0][2] != 0 && macroGrilles[0][2] != 3 && macroGrilles[0][2] == macroGrilles[1][1] && macroGrilles[1][1] == macroGrilles[2][0])
        return macroGrilles[0][2];

    return 0; // Pas encore de vainqueur
}

bool isFinish() const {
    // La partie est finie si quelqu'un a gagné la macro-grille
    if (getWinner() != 0) return true;

    // Ou si plus aucun coup n'est possible (plateau plein)
    if (getCoupsPossibles().empty()) return true;

    return false;
}
};

#endif // PLATEAU_H
