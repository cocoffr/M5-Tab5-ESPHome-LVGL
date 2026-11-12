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
  // Une surtension déclenche une sonde et coupe immédiatement le chargeur.
  EtatChargeur e;
  chargeur_demarrer(e, 0, false, true, false, 1000);
  chargeur_tick(e, 0, false, 9001);
  chargeur_lecture(e, 7.80f, 9100);
  assert(chargeur_tick(e, 0, false, 9200).allumer);
  chargeur_lecture(e, 8.60f, 9300);
  assert(!e.allume);
  assert(e.sonde);
  assert(e.securite_sonde == CauseSecurite::TENSION);
  assert(!chargeur_tick(e, 0, false, 9400).allumer);
  chargeur_tick(e, 0, false, 17400);
  chargeur_lecture(e, 7.80f, 17500);
  assert(e.electrique_vu);

  // Limite 80 % : une batterie déjà au-dessus du seuil ne doit pas
  // démarrer une nouvelle charge.
  EtatChargeur limit;
  chargeur_demarrer(limit, 0, false, true, false, 1000);
  chargeur_tick(limit, 1, false, 9001);
  chargeur_lecture(limit, 8.20f, 9100);
  assert(!chargeur_tick(limit, 1, false, 9200).allumer);
  // Absence de batterie : aucune activation même après plusieurs cycles.
  EtatChargeur absent;
  chargeur_demarrer(absent, static_cast<uint8_t>(MemoireBatterie::INCONNUE),
                   false, true, false, 1000);
  chargeur_tick(absent, 0, false, 9001);
  chargeur_lecture(absent, 1.90f, 9100);
  for (uint32_t ms = 9200; ms < 30000; ms += 1000) {
    assert(!chargeur_tick(absent, 0, false, ms).allumer);
  }

  // Redémarrage avec verrou thermique mémorisé : ne jamais recharger.
  EtatChargeur locked;
  chargeur_demarrer(locked, static_cast<uint8_t>(MemoireBatterie::VUE),
                   true, true, true, 1000);
  assert(chargeur_securite_posee(locked));
  chargeur_tick(locked, 0, true, 9001);
  chargeur_lecture(locked, 7.70f, 9100);
  assert(!chargeur_tick(locked, 0, true, 9200).allumer);

  // Lecture invalide : ne pas inventer une présence de batterie.
  EtatChargeur invalid;
  chargeur_demarrer(invalid, static_cast<uint8_t>(MemoireBatterie::INCONNUE),
                   false, true, false, 1000);
  chargeur_tick(invalid, 0, false, 9001);
  chargeur_lecture(invalid, NAN, 9100);
  assert(invalid.presence == PresenceBatterie::INCONNUE);
  assert(!chargeur_tick(invalid, 0, false, 9200).allumer);
  std::cout << "Tests batterie OK\\n";
}
