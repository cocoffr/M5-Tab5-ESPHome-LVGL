/**
 * [AI-CONTEXT]
 * @file tab5_custom.cpp
 * @role Globals partagés de la couche C++ et carte de ses unités. Depuis le 08/09/2026
 *       (lot (e) de l'audit du 06/09), la logique vit dans une unité par responsabilité,
 *       toutes déclarées dans tab5_custom.h (unique en-tête public, inchangé) :
 *         tab5_text.cpp      UTF-8 / mojibake, store des alertes rejetées, libellés de jours,
 *                            journal d'un payload refusé (payload_refuse, lot L5)
 *         tab5_forecast.cpp  icônes et couleurs météo, parsing bulk jours/heures, tuiles
 *         tab5_central.cpp   carte centrale, alertes HA, pagination au swipe, planning tap
 *         tab5_services.cpp  logique des services HA (volet, vigilance, pluie, clim, planning)
 *         tab5_assist.cpp    popup Assistant (Markdown, états), décision du mot de réveil
 *         tab5_cards.cpp     cartes plantes/pots, température colorée (la clim : tab5_clim.cpp
 *                            depuis le 08/10/2026 ; la lumière : les tuiles, ADR-0023)
 *         tab5_console.cpp   console système (status, volume, diagnostics)
 *         tab5_anim.cpp      animations, inactivité, rouleaux (icône météo, horloge), boutons
 *         tab5_calendar.cpp  popup calendrier mensuel
 *       tab5_internal.h déclare les quelques helpers partagés entre unités.
 * @regle_absolue Aucune autre partie du code (YAML ou autre) ne doit appeler lv_obj_set_*
 *                directement : tout widget LVGL est mis à jour via un helper de ces unités,
 *                déclaré dans tab5_custom.h (ADR-0006, garde-fou tools/check_tab5_code_rules.py).
 * @memory_constraint Pas de std::string dans une boucle de parsing : découper un char* en place
 *       (`split_fields()` de tab5_core.h garde les champs vides, `strtok_r` les fusionne) ;
 *                    la SRAM est critique (768KB), privilégier le stack (char buf[32]).
 * @ai_instruction Un nouveau capteur = une fonction `update_mon_capteur_ui(lv_obj_t*, float)`
 *                 dans l'unité de sa responsabilité + sa déclaration dans tab5_custom.h,
 *                 appelée depuis le YAML. Ne génère pas de code LVGL dans le YAML.
 */
#include "tab5_custom.h"

// Contexte global carte centrale (initialise au boot via YAML on_boot).
CentralPanelCtx g_central_ctx;

// Tableaux globaux des slots meteo (initialises au boot via YAML on_boot).
WeatherDaySlot g_day_slots[5];
WeatherHourSlot g_hour_slots[5];

// Bandeaux d'alerte HA de la carte centrale (script tab5_ha_alert_slots_init).
HaAlertSlotUI g_ha_alert_slots[kHaAlertSlotCount];

// Donnees planning/previsions remplies par parse_and_update_*_bulk (tab5_forecast.cpp),
// lues par la carte centrale, les services et le calendrier.
DayForecastData cal_jours_data[15];
HourForecastData cal_heures_data[15];
int32_t cal_jours_anchor_day = -1;  // jour local de cal_jours_data[0], -1 = inconnu

void reveil_accueil_tick(lv_obj_t* heure, lv_obj_t* date_label,
                        lv_obj_t* luminosite, lv_obj_t* volume,
                        lv_obj_t* alarme1, lv_obj_t* alarme2,
                        bool heure_valide, int hh, int mm, int jour, int mois, int annee,
                        float luminosite_fraction, float volume_pourcent,
                        bool alarme1_active, bool alarme2_active) {
  char texte[48];
  if (heure_valide) {
    snprintf(texte, sizeof(texte), "%02d:%02d", hh, mm);
    lv_label_set_text(heure, texte);
    snprintf(texte, sizeof(texte), "%02d/%02d/%04d", jour, mois, annee);
    lv_label_set_text(date_label, texte);
  }
  snprintf(texte, sizeof(texte), "%d/10",
           std::max(1, std::min(10, static_cast<int>(roundf(luminosite_fraction * 10.0f)))));
  lv_label_set_text(luminosite, texte);
  snprintf(texte, sizeof(texte), "%d/10",
           std::max(1, std::min(10, static_cast<int>(roundf(volume_pourcent / 10.0f)))));
  lv_label_set_text(volume, texte);
  lv_label_set_text(alarme1, alarme1_active ? "ON" : "OFF");
  lv_label_set_text(alarme2, alarme2_active ? "ON" : "OFF");
  lv_obj_set_style_text_color(alarme1,
                              lv_color_hex(alarme1_active ? UIColor.SUCCESS : UIColor.TEXT_DIM),
                              LV_PART_MAIN);
  lv_obj_set_style_text_color(alarme2,
                              lv_color_hex(alarme2_active ? UIColor.SUCCESS : UIColor.ERROR),
                              LV_PART_MAIN);
}
