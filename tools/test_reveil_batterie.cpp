// Tests natifs du contrôleur de charge, sans activation de sorties physiques.
#include "Tab5/socle/tab5_batterie.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  assert(std::isnan(batterie_niveau_pct(NAN)));
  assert(batterie_niveau_pct(6.60f) == 0.0f);
  assert(batterie_niveau_pct(8.35f) == 100.0f);
  assert(batterie_niveau_pct(8.05f) >= 80.0f);
  assert(!charge_rapide_voulue(1, false));
  assert(charge_rapide_voulue(1, true));
  assert(!charge_rapide_voulue(0, true));
  assert(std::isnan(batterie_puissance_w(8.0f, 0.5f, false)));
  assert(std::fabs(batterie_puissance_w(8.0f, 0.5f, true) - 4.0f) < 0.01f);

  EtatChargeur c;
  chargeur_demarrer(c, static_cast<uint8_t>(MemoireBatterie::INCONNUE),
                   false, true, false, 1000);
  assert(!chargeur_tick(c, 0, false, 2000).allumer);
  assert(!chargeur_tick(c, 0, false, 9001).allumer);
  chargeur_lecture(c, 1.90f, 9100);
  assert(c.presence == PresenceBatterie::ABSENTE);
  assert(!chargeur_tick(c, 0, false, 9200).allumer);

  EtatChargeur d;
  chargeur_demarrer(d, 0, true, true, false, 1000);
  chargeur_tick(d, 0, true, 9001);
  chargeur_lecture(d, 7.70f, 9100);
  assert(d.presence == PresenceBatterie::PRESENTE);
  assert(chargeur_tick(d, 0, true, 9200).allumer);
  assert(chargeur_surveillance_thermique(d, 56.0f, 9300));
  assert(chargeur_securite_posee(d));
  assert(!chargeur_tick(d, 0, true, 9400).allumer);
  assert(!chargeur_tick(d, 0, true, 9400 + 15u*60u*1000u - 1).allumer);
  chargeur_surveillance_thermique(d, 40.0f, 9400 + 15u*60u*1000u);
  assert(!d.verrou_thermique);
  std::cout << "Tests batterie OK\\n";
}
