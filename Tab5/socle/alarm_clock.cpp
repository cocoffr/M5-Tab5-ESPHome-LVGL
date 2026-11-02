/**
 * [AI-CONTEXT]
 * @file alarm_clock.cpp
 * @role Implémentation du moteur de réveil + annonce des rendez-vous — logique
 *       PURE (ni ESPHome ni LVGL), compilée aussi par tools/test_alarm_clock.cpp
 *       (g++ en CI). Le rendu LVGL est dans alarm_render.cpp (lot 8b, 25/09/2026).
 *       Contrat et justifications d'architecture : voir alarm_clock.h.
 */
#include "alarm_clock.h"
#include "tab5_i18n.h"
#include "tab5_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

AlarmCfg g_alarm_cfg;

// ═══════════════════════════════════════════════════════════════════════════
// Helpers de bas niveau
// ═══════════════════════════════════════════════════════════════════════════

static int clamp_i(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// "HH:MM-HH:MM" -> minutes depuis minuit du DÉBUT, -1 si illisible.
// Le champ vient de `cal_jours_data[].heures_ouverture`, rempli par
// parse_and_update_jours_bulk() depuis l'automation HA (section 5). Il est vide
// quand la journée est du repos, mais AUSSI quand le service commence la veille
// (le gabarit HA ne remplit les heures que si `target_date in ev_start`) — d'où
// le -1 distinct de « pas de travail », que l'appelant traite différemment.
static int parse_shift_start(const std::string& h) { return hhmm_minutes(h, 0); }

// "HH:MM-HH:MM" -> minutes depuis minuit de la FIN (la « fermeture »), -1 sinon.
static int parse_shift_end(const std::string& h) {
  if (h.size() < 11 || h[5] != '-') return -1;
  return hhmm_minutes(h, 6);
}

// Epoch local d'une heure murale précise à J+offset. On construit le tm complet
// puis mktime() : surtout PAS « minuit + n×60 », qui décale d'une heure les deux
// nuits de bascule heure d'été/hiver. Un réveil se règle sur l'heure de
// l'horloge murale, pas sur une durée depuis minuit.
static time_t epoch_at(int day_offset, int minute_of_day) {
  struct tm t;
  if (!local_day_from_offset(day_offset, t)) return 0;
  t.tm_hour = minute_of_day / 60;
  t.tm_min = minute_of_day % 60;
  t.tm_sec = 0;
  t.tm_isdst = -1;
  const time_t r = mktime(&t);
  return (r == static_cast<time_t>(-1)) ? 0 : r;
}

// bit 0 = lundi … bit 6 = dimanche (tm_wday : 0 = dimanche).
static bool day_selected(uint8_t mask, int tm_wday) {
  const int bit = (tm_wday + 6) % 7;
  return (mask >> bit) & 1;
}

// cal_index_for_offset() : tab5_core.cpp (partagée avec le planning de la carte centrale).

// Prêt = calendrier reçu ET couvrant aujourd'hui.
bool alarm_calendar_ready() {
  return !cal_jours_data[0].nom_jour.empty() && cal_index_for_offset(0) >= 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Deux moteurs indépendants : même calendrier, deux configurations et deux états.
// ═══════════════════════════════════════════════════════════════════════════
AlarmCfg g_alarm_cfg_2;

namespace {
constexpr int ALARM_COUNT = 2;
struct AlarmRuntime {
  bool dirty = true;
  time_t cached_at = 0;
  time_t next_ts = 0;
  int next_offset = -1;
  int next_min = -1;
  time_t skip_before = 0;
  time_t snooze_until = 0;
  int snooze_count = 0;
  time_t last_tick = 0;
};
AlarmRuntime s_alarm[ALARM_COUNT];

int valid_alarm_id(int id) { return id == 1 ? 1 : 0; }
AlarmRuntime& runtime(int id) { return s_alarm[valid_alarm_id(id)]; }
const AlarmCfg& config(int id) { return valid_alarm_id(id) == 1 ? g_alarm_cfg_2 : g_alarm_cfg; }
}  // namespace

void alarm_invalidate_for(int alarm_id) { runtime(alarm_id).dirty = true; }
void alarm_invalidate() { alarm_invalidate_for(0); alarm_invalidate_for(1); }

uint32_t alarm_skip_floor_for(int alarm_id) {
  return static_cast<uint32_t>(runtime(alarm_id).skip_before);
}
uint32_t alarm_skip_floor() { return alarm_skip_floor_for(0); }
void alarm_set_skip_floor_for(int alarm_id, uint32_t v) {
  runtime(alarm_id).skip_before = static_cast<time_t>(v);
  runtime(alarm_id).dirty = true;
}
void alarm_set_skip_floor(uint32_t v) { alarm_set_skip_floor_for(0, v); }

// Heure de sonnerie retenue pour le jour J+offset, en minutes depuis minuit.
static int ring_minute_for_day(int alarm_id, int offset, const struct tm& day) {
  const AlarmCfg& c = config(alarm_id);
  const int idx = cal_index_for_offset(offset);
  const bool cal_ok = alarm_calendar_ready() && idx >= 0;
  const bool travaille = cal_ok && !cal_jours_data[idx].est_repos;
  const int fixe_si_coche = day_selected(c.days_mask, day.tm_wday) ? c.fixed_min : -1;
  if (c.mode == AlarmMode::FIXE || !cal_ok) return fixe_si_coche;
  if (!travaille) return (c.rest_mode == AlarmRepos::FIXE) ? fixe_si_coche : -1;
  if (c.mode == AlarmMode::TRAVAIL) return c.fixed_min;
  const int start = parse_shift_start(cal_jours_data[idx].heures_ouverture);
  if (start < 0) return c.fixed_min;
  int m = clamp_i(start - c.lead_min, c.earliest_min, c.latest_min);
  if (c.rest_hours > 0 && idx >= 1) {
    const int fin_veille = parse_shift_end(cal_jours_data[idx - 1].heures_ouverture);
    if (fin_veille >= 0 && !cal_jours_data[idx - 1].est_repos) {
      const int plancher = fin_veille - 1440 + c.rest_hours * 60;
      if (plancher > m) m = clamp_i(plancher, c.earliest_min, c.latest_min);
    }
  }
  return m;
}

time_t alarm_next_ring_for(int alarm_id, time_t now) {
  if (now <= 0) return 0;
  AlarmRuntime& s = runtime(alarm_id);
  const AlarmCfg& c = config(alarm_id);
  if (!s.dirty && (now - s.cached_at) < 60 && s.cached_at != 0) return s.next_ts;
  s.dirty = false; s.cached_at = now; s.next_ts = 0; s.next_offset = -1; s.next_min = -1;
  if (!c.enabled) return 0;
  const time_t not_before = (s.last_tick > 0) ? s.last_tick : (now - ALARM_GRACE_S);
  const time_t floor_ts = (not_before > s.skip_before) ? not_before : s.skip_before;
  for (int offset = 0; offset <= 8; offset++) {
    struct tm day;
    if (!local_day_from_offset(offset, day)) return 0;
    const int m = ring_minute_for_day(alarm_id, offset, day);
    if (m < 0) continue;
    const time_t ts = epoch_at(offset, m);
    if (ts == 0 || ts <= floor_ts) continue;
    s.next_ts = ts; s.next_offset = offset; s.next_min = m;
    return ts;
  }
  return 0;
}
time_t alarm_next_ring(time_t now) { return alarm_next_ring_for(0, now); }

static bool alarm_due_for_(int alarm_id, time_t now) {
  AlarmRuntime& s = runtime(alarm_id);
  const AlarmCfg& c = config(alarm_id);
  if (s.snooze_until > 0) {
    if (now < s.snooze_until) return false;
    s.snooze_until = 0;
    return true;
  }
  if (!c.enabled) return false;
  const time_t next = alarm_next_ring_for(alarm_id, now);
  if (next == 0 || now < next) return false;
  s.skip_before = next;
  s.snooze_count = 0;
  s.dirty = true;
  return true;
}
bool alarm_due_for(int alarm_id, time_t now) {
  if (now <= 0) return false;
  AlarmRuntime& s = runtime(alarm_id);
  const bool ring = alarm_due_for_(alarm_id, now);
  s.last_tick = now;
  return ring;
}
bool alarm_due(time_t now) { return alarm_due_for(0, now); }

void alarm_snooze_for(int alarm_id, time_t now, int minutes) {
  if (minutes < 1) minutes = 1;
  AlarmRuntime& s = runtime(alarm_id);
  s.snooze_count++;
  s.snooze_until = now + static_cast<time_t>(minutes) * 60;
}
void alarm_snooze(time_t now, int minutes) { alarm_snooze_for(0, now, minutes); }
int alarm_snooze_count_for(int alarm_id) { return runtime(alarm_id).snooze_count; }
int alarm_snooze_count() { return alarm_snooze_count_for(0); }

void alarm_dismiss_for(int alarm_id, time_t now) {
  AlarmRuntime& s = runtime(alarm_id);
  s.snooze_until = 0; s.snooze_count = 0;
  if (now > s.skip_before) s.skip_before = now;
  s.dirty = true;
}
void alarm_dismiss(time_t now) { alarm_dismiss_for(0, now); }

void alarm_reset_state() {
  for (auto& s : s_alarm) s = AlarmRuntime{};
}

// ═══════════════════════════════════════════════════════════════════════════
// Libellés
// ═══════════════════════════════════════════════════════════════════════════

void alarm_hhmm(int minute_of_day, char* out, size_t n) {
  // Borné à une journée : -O2 sait alors que chaque champ tient sur deux chiffres
  // (sinon -Wformat-truncation). Les appelants passent toujours une heure valide.
  const unsigned m = static_cast<unsigned>(minute_of_day) % 1440u;
  snprintf(out, n, "%02u:%02u", m / 60u, m % 60u);
}

std::string alarm_next_label(time_t now) {
  // Une répétition en cours PASSE DEVANT le prochain réveil calculé : c'est elle
  // qui va sonner. Annoncer « Demain 05:15 » pendant qu'une répétition court
  // dans 6 minutes serait le seul moment où cet écran mentirait.
  if (runtime(0).snooze_until > now) {
    struct tm t;
    if (localtime_r(&runtime(0).snooze_until, &t) != nullptr) {
      const int reste = static_cast<int>((runtime(0).snooze_until - now + 59) / 60);
      char buf[96];
      snprintf(buf, sizeof(buf), tr("R\xC3\xA9p\xC3\xA9tition %02d:%02d (%d min)"), t.tm_hour, t.tm_min, reste);
      return std::string(buf);
    }
  }
  if (!config(0).enabled) return tr("D\xC3\xA9sactiv\xC3\xA9");
  if (alarm_next_ring_for(0, now) == 0) return tr("Aucune sonnerie pr\xC3\xA9vue");

  char h[8];
  alarm_hhmm(runtime(0).next_min, h, sizeof(h));

  if (runtime(0).next_offset == 0) {
    return tr_fill("Aujourd'hui {heure}", {{"heure", h}});
  }
  if (runtime(0).next_offset == 1) {
    return tr_fill("Demain {heure}", {{"heure", h}});
  }
  struct tm day;
  if (!local_day_from_offset(runtime(0).next_offset, day)) return std::string(h);
  // Majuscule initiale sur le jour : c'est un début de libellé.
  return fr_capitalized(day_long_utf8(day.tm_wday)) + " " + h;
}

std::string alarm_next_detail(time_t now) {
  if (runtime(0).snooze_until > now) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             tr("R\xC3\xA9p\xC3\xA9tition n\xC2\xB0%d \xC2\xB7 « Arr\xC3\xAAter » pour reprendre le cycle normal"),
             runtime(0).snooze_count);
    return std::string(buf);
  }
  if (!config(0).enabled) return tr("Touchez l'interrupteur pour l'armer");
  if (alarm_next_ring_for(0, now) == 0) return tr("Aucun jour retenu dans les 8 prochains");

  // « jeudi 1er octobre » : même libellé que les titres de jours (tab5_core.cpp),
  // qui connaît le seul ordinal du français. Vide si le jour est hors bornes.
  const std::string date = format_long_day_label(runtime(0).next_offset);
  if (date.empty()) return "";

  if (!alarm_calendar_ready()) {
    return date + " \xC2\xB7 " + tr("en attente du calendrier");
  }
  // Jour non couvert (données trop vieilles pour aller jusque-là) : la sonnerie a
  // été calculée sur l'heure fixe, comme sans calendrier.
  const int idx = cal_index_for_offset(runtime(0).next_offset);
  if (idx < 0) {
    return date + " \xC2\xB7 " + tr("en attente du calendrier");
  }
  if (cal_jours_data[idx].est_repos) {
    return date + " \xC2\xB7 " + tr("repos");
  }
  const std::string& h = cal_jours_data[idx].heures_ouverture;
  if (h.size() >= 11) {
    // « Travail 06:45 – 15:30 » : tiret demi-cadratin UTF-8, comme le popup
    // calendrier (cal_render_day_detail).
    return date + " \xC2\xB7 " + tr("Travail") + " " + h.substr(0, 5) + " \xE2\x80\x93 " + h.substr(6, 5);
  }
  return date + " \xC2\xB7 " + tr("Travail (horaire inconnu)");
}

// ═══════════════════════════════════════════════════════════════════════════
// Préréglages de jours
// ═══════════════════════════════════════════════════════════════════════════
namespace {
// Masque de chaque préréglage, dans l'ordre des options du select « Réveil : jours »
// (tab5-alarm.yaml) : Every day, Monday-Friday, Monday-Saturday, Weekend, Custom. Le
// select est lu et publié par son INDEX (24/10/2026, ADR-0070 : ses options sont le
// texte de HA, en anglais ; les noms français de cette table sont partis avec).
// -1 = « Custom » (« Personnalisé ») : sélectionner cette entrée ne change rien. Elle
// DOIT rester la dernière : alarm_days_preset_index() s'en sert comme valeur de repli
// quand aucun masque ne correspond.
struct DaysPreset {
    int mask;
};
const DaysPreset kPresets[ALARM_DAYS_PRESET_COUNT] = {{0x7F}, {0x1F}, {0x3F}, {0x60}, {-1}};
}  // namespace

int alarm_days_preset_mask(int idx) {
  return kPresets[clamp_i(idx, 0, ALARM_DAYS_PRESET_COUNT - 1)].mask;
}
int alarm_days_preset_index(uint8_t mask) {
  for (int i = 0; i < ALARM_DAYS_PRESET_COUNT; i++) {
    if (kPresets[i].mask >= 0 && static_cast<uint8_t>(kPresets[i].mask) == mask) return i;
  }
  return ALARM_DAYS_PRESET_COUNT - 1;  // « Personnalisé »
}

// ═══════════════════════════════════════════════════════════════════════════
// Mélodies RTTTL
// ═══════════════════════════════════════════════════════════════════════════
// Format : nom:d=durée par défaut,o=octave par défaut,b=tempo:notes.
// Éditer une ligne suffit — aucune police, aucun octet de flash, aucun réglage
// HA ne dépend de leur contenu (seul le NOMBRE d'entrées est repris par le
// select exposé à HA, cf. ALARM_MELODY_COUNT).
// Une ligne par option du select « Réveil : sonnerie », dans son ordre : Gentle (Douce),
// Classic (Classique), Insistent (Insistante), Chime (Carillon). Le select est lu par son
// INDEX (g_alarm_cfg.melody) : ses options sont le texte de HA (anglais depuis le
// 24/10/2026, ADR-0070) ; le nom en tête de partition ne sert qu'au format RTTTL.
namespace {
// Pas de durée : le cycle de sonnerie s'arrête sur `rtttl.is_playing`, pas sur
// une estimation (le champ `ms` et alarm_melody_ms(), jamais lus, sont retirés
// le 25/09/2026, audit lot 8a).
struct Melody {
    const char* score;
};
const Melody kMelodies[ALARM_MELODY_COUNT] = {
    {"Douce:d=8,o=6,b=92:c,e,g,c7,p,g,e,c,2p"},
    {"Reveil:d=16,o=6,b=140:c7,c7,p,c7,c7,4p,c7,c7,p,c7,c7,4p"},
    {"Urgence:d=32,o=7,b=180:c,c,c,c,8p,c,c,c,c,8p,c,c,c,c,4p"},
    {"Carillon:d=4,o=5,b=90:e,c,d,1g4,2p,g4,d,e,1c"},
};
}  // namespace

const char* alarm_melody_rtttl(int idx) {
  return kMelodies[clamp_i(idx, 0, ALARM_MELODY_COUNT - 1)].score;
}

// Crescendo : 35 % du volume cible au premier cycle, puis +13 points par cycle
// jusqu'au plein volume au 6e. Le plancher n'est pas 0 : un réveil qu'on
// n'entend pas les 20 premières secondes ne réveille personne.
float alarm_ring_gain(float base, int cycle, bool crescendo) {
  if (base < 0.0f) base = 0.0f;
  if (base > 1.0f) base = 1.0f;
  if (!crescendo) return base;
  constexpr float FLOOR = 0.35f;
  constexpr float STEP = 0.13f;
  float f = FLOOR + STEP * static_cast<float>(cycle < 0 ? 0 : cycle);
  if (f > 1.0f) f = 1.0f;
  return base * f;
}

// ═══════════════════════════════════════════════════════════════════════════
// Rendez-vous
// ═══════════════════════════════════════════════════════════════════════════
namespace {
struct Rdv {
  time_t start = 0;
  bool announced = false;
  std::string titre;
};
Rdv s_rdv[ALARM_RDV_MAX];
int s_rdv_n = 0;
}  // namespace


void rdv_clear() {
  for (int i = 0; i < ALARM_RDV_MAX; i++) {
    s_rdv[i].start = 0;
    s_rdv[i].announced = false;
    s_rdv[i].titre.clear();
  }
  s_rdv_n = 0;
}

void rdv_store(const std::string& payload) {
  // On mémorise ce qui a DÉJÀ été annoncé avant d'écraser la liste : HA repousse
  // la même liste toutes les 5 minutes, et sans cet appariement par epoch chaque
  // poussée ferait ré-annoncer le rendez-vous en cours en boucle.
  time_t done[ALARM_RDV_MAX];
  int done_n = 0;
  for (int i = 0; i < s_rdv_n; i++) {
    if (s_rdv[i].announced && done_n < ALARM_RDV_MAX) done[done_n++] = s_rdv[i].start;
  }

  rdv_clear();
  size_t pos = 0;
  while (pos < payload.size() && s_rdv_n < ALARM_RDV_MAX) {
    const size_t rec_end = payload.find('~', pos);
    const std::string rec = payload.substr(pos, rec_end == std::string::npos ? std::string::npos : rec_end - pos);
    pos = (rec_end == std::string::npos) ? payload.size() : rec_end + 1;
    if (rec.empty()) continue;

    const size_t bar = rec.find('|');
    if (bar == std::string::npos) continue;
    const time_t ts = static_cast<time_t>(strtoll(rec.substr(0, bar).c_str(), nullptr, 10));
    if (ts <= 0) continue;

    Rdv& r = s_rdv[s_rdv_n];
    r.start = ts;
    r.titre = rec.substr(bar + 1);
    r.announced = false;
    for (int k = 0; k < done_n; k++) {
      if (done[k] == ts) { r.announced = true; break; }
    }
    s_rdv_n++;
  }
}

// « 14:30 » depuis un epoch, en heure locale.
static bool rdv_hhmm(time_t ts, char* out, size_t n) {
  struct tm t;
  if (localtime_r(&ts, &t) == nullptr) return false;
  snprintf(out, n, "%02d:%02d", t.tm_hour, t.tm_min);
  return true;
}

bool rdv_due(time_t now, int lead_min, std::string& out_screen, std::string& out_speech) {
  if (now <= 0 || lead_min < 0) return false;
  const time_t lead = static_cast<time_t>(lead_min) * 60;

  for (int i = 0; i < s_rdv_n; i++) {
    Rdv& r = s_rdv[i];
    if (r.announced || r.start <= 0) continue;
    if (now < r.start - lead) continue;
    // Rendez-vous déjà commencé depuis plus d'une minute quand on le découvre
    // (poussée tardive, appareil rallumé) : on le marque lu sans rien annoncer.
    if (now > r.start + 60) { r.announced = true; continue; }
    r.announced = true;

    char h[8];
    if (!rdv_hhmm(r.start, h, sizeof(h))) return false;
    const int reste = static_cast<int>((r.start - now + 59) / 60);  // minutes, arrondi au-dessus

    char scr[192];
    if (reste > 0) {
      snprintf(scr, sizeof(scr), tr("%s \xC2\xB7 %s (dans %d min)"), h, r.titre.c_str(), reste);
    } else {
      snprintf(scr, sizeof(scr), tr("%s \xC2\xB7 %s (maintenant)"), h, r.titre.c_str());
    }
    out_screen = scr;

    // Version parlée : « 14 h 30 » plutôt que « 14:30 » (les moteurs TTS
    // français lisent mal le deux-points), et une phrase complète ponctuée pour
    // que la prosodie ne parte pas en liste.
    char spk[224];
    const int hhmm = hhmm_minutes(h);  // écrit par rdv_hhmm : toujours lisible
    const int hh = hhmm < 0 ? 0 : hhmm / 60;
    const int mm = hhmm < 0 ? 0 : hhmm % 60;
    char heure_parlee[24];
    if (mm == 0) {
      snprintf(heure_parlee, sizeof(heure_parlee), tr("%d heures"), hh);
    } else {
      snprintf(heure_parlee, sizeof(heure_parlee), tr("%d heures %d"), hh, mm);
    }
    if (reste > 0) {
      // Deux phrases plutôt qu'un « s » ajouté : le pluriel n'est pas un « s » dans
      // toutes les langues (Minute → Minuten, minuut → minuten).
      snprintf(spk, sizeof(spk),
               reste > 1 ? tr("Rappel : %s, \xC3\xA0 %s, dans %d minutes.")
                         : tr("Rappel : %s, \xC3\xA0 %s, dans %d minute."),
               r.titre.c_str(), heure_parlee, reste);
    } else {
      snprintf(spk, sizeof(spk), tr("Rappel : %s, c'est maintenant."), r.titre.c_str());
    }
    out_speech = spk;
    return true;
  }
  return false;
}

std::string rdv_next_label(time_t now) {
  const Rdv* best = nullptr;
  for (int i = 0; i < s_rdv_n; i++) {
    if (s_rdv[i].start <= now) continue;
    if (best == nullptr || s_rdv[i].start < best->start) best = &s_rdv[i];
  }
  if (best == nullptr) return "";
  char h[8];
  if (!rdv_hhmm(best->start, h, sizeof(h))) return "";
  return std::string(h) + " \xC2\xB7 " + best->titre;
}
