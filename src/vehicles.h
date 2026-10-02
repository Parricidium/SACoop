// Vehicules partages (vehicles.cpp).
#pragma once
#include <stdint.h>

void VehiclesMissionEnded();                     // invite : fin de mission de l'hote (vehicule pose pour lui rendu au jeu)
void VehiclesFrame();                            // chaque tour de la boucle en partie (coop.cpp)
uint32_t HostVehicleId(void *veh, bool occupied, bool ambient);   // hote : vehicule de mission / d'un personnage partage
void HostRegisterMissionVehicles();
void HostRegisterAmbientVehicles();              // hote : vehicules ordinaires pres des invites partages
bool IsNetVehicle(void *veh);
bool IsRemoteVehicle(void *veh);   // copie du vehicule d'un autre joueur
uint32_t LocalVehicleId(void *veh, bool driver); // identifiant reseau du vehicule occupe par le joueur local
void *NetVehicleByOwnerRef(int owner, int ref);  // copie d'un vehicule par son handle de script chez owner
int VehicleRef(void *veh);
void *AnyMissionVehicleCopy();                   // autotests                       // handle de script (reference de pool) d'un vehicule
void *NetVehicleById(uint32_t id);               // vehicule (le notre ou une copie) de cet identifiant, ou nullptr
bool PuppetInVehicle(void *veh);                 // coop.cpp : un pantin est-il dans ce vehicule ?
