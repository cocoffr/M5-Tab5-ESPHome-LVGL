# Portage de la batterie du Tab5 d'origine vers le réveil

Branche isolée : `feature/reveil-batterie-portage` (base `23e44dc`).
Ce document fixe le périmètre et les conditions de validation. **Ne pas activer CHG_EN sans essais matériels.**

## Source de vérité
- `Tab5/socle/tab5_batterie.h` : présence 2S, courbe de décharge mesurée, limites 80/100 %, machine d'états, sécurités.
- `Tab5/ecran/tab5_batterie.cpp` : intégration NVS, surveillance, journalisation.
- `Tab5/tab5-sensors-diagnostics.yaml` : INA226, GPIO de charge, sélecteurs, événements HA et cadence des mesures.
- `tests/test_batterie.py` : invariants de sécurité.
- `reveil-minimal.yaml` : écran et mesures déjà amorcés, contrôleur actuellement en simulation.

## État observé dans le réveil
- INA226 à 0x41, tension/courant, température CPU, diagnostic de fraîcheur : présents.
- Page Batterie : niveau, tension, puissance, limite de charge, mode de charge, préférences économie et batterie déclarée : présents.
- Contrôleur d'origine inclus et exécuté en simulation ; `kChargeMaterielleValidee = false` : chargeur matériel **désactivé**.
- La déclaration manuelle de présence n'est pas équivalente à une détection physique fiable.
- La logique d'économie Wi-Fi est encore une préférence, non un contrôle matériel.
- La correspondance complète des états, alertes, icônes et sécurités avec le projet d'origine reste à vérifier.

## Ordre de travail
1. Comparer précisément les entités, commandes et widgets du projet d'origine et du réveil.
2. Porter la détection réelle de présence, l'état secteur/batterie, le pourcentage selon la courbe mesurée et les alertes, en gardant la charge coupée.
3. Reprendre les réglages d'origine et leurs valeurs persistantes : 80/100 %, charge classique/rapide, batterie montée, économie d'énergie.
4. Porter les protections : sonde chargeur coupé, contrôle courant/tension, verrou thermique NVS, verrou électrique et événements HA.
5. Tester en logiciel les transitions sans batterie, batterie insérée, charge complète, seuil 80 %, défaut INA226, défaut thermique, redémarrage.
6. Compiler sur la branche batterie, puis effectuer les essais physiques sur ST7121 avec une batterie compatible avant toute autorisation de CHG_EN.
7. Intégrer dans la branche réveil seulement après réussite des validations.

## Sécurité non négociable
- Démarrage avec CHG_EN et charge rapide coupés.
- Aucune activation sur simple interrupteur « batterie montée ».
- Sonde de présence chargeur coupé, mesures fraîches et température valide exigées.
- Une erreur de mesure ou un verrou coupe le chargeur.
- Pas de modification de `kChargeMaterielleValidee` avant validation matérielle explicite.
