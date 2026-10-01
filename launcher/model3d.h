// SACoop - lanceur : modeles du jeu en 3D (apercu des tenues, des mods, portraits du salon). Porte de VCCoop.
// Lit les fichiers du jeu du joueur (models\gta3.img, models\player.img, data\peds.ide, data\vehicles.ide, et
// SACoop\mods qui les remplacent) : aucun fichier de Rockstar n'est livre avec le mod. Rendu logiciel (pas de carte graphique a
// partager avec une fenetre transparente), dans un tampon ARGB premultiplie.
#pragma once
#include <string>
#include <vector>
#include <stdint.h>

struct Model3D;

// Ouvre le catalogue du jeu (dossier avec \ final). Faux si models\gta3.img est introuvable.
bool ImgOpen(const std::wstring &gameDir);
// Nom du modele d'un numero de personnage (peds.ide), "" si inconnu.
std::string PedModelName(int id);
bool ModelExists(const std::string &name);   // .dff et .txd presents (jeu ou mods)
std::vector<int> PedIds();                    // pietons de peds.ide (1-288) presents dans le jeu
// Modele et textures du meme nom ("player" : CJ assemble de ses vetements). NULL si introuvable ou illisible.
Model3D *ModelLoad(const std::string &name);
// Modele d'un fichier .dff (mod), textures du .txd donne ou, a defaut, celles du jeu du meme nom.
Model3D *ModelLoadPath(const std::wstring &dffPath, const std::wstring &txdPath);
void ModelFree(Model3D *m);
std::string ModelInfo(const Model3D *m);   // diagnostic : textures (taille, couleur moyenne), materiaux
void ModelBounds(const Model3D *m, float *lo, float *hi);
// Rendu dans out (w x h, ARGB premultiplie, fond transparent). yaw : rotation autour de la verticale (0 = de face).
// style : 0 = personnage en pied, 1 = portrait (visage), 2 = objet ou vehicule (toute la boite, vue du dessus).
void ModelRender(const Model3D *m, uint32_t *out, int w, int h, float yaw, int style);
