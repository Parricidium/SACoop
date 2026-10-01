// Vehicules partages (vehicles.cpp).
#pragma once
#include <stdint.h>

void VehiclesFrame();                            // chaque tour de la boucle en partie (coop.cpp)
uint32_t HostVehicleId(void *veh, bool occupied);   // hote : vehicule de mission / d'un personnage de mission
void HostRegisterMissionVehicles();
uint32_t LocalVehicleId(void *veh, bool driver); // identifiant reseau du vehicule occupe par le joueur local
void *NetVehicleById(uint32_t id);               // vehicule (le notre ou une copie) de cet identifiant, ou nullptr
bool PuppetInVehicle(void *veh);                 // coop.cpp : un pantin est-il dans ce vehicule ?
