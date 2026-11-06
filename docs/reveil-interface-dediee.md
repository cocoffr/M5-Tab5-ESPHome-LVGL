# Interface dédiée du réveil de chevet — ST7121

## Décision validée

L'accueil Axellum doit être remplacé, et non simplement complété par un popup.
Le Tab5 doit démarrer directement sur un écran de réveil en français, paysage,
retourné à 180°.

## Composition de l'accueil (1280 × 720)

- Heure HH:MM : chiffres blancs, **largeur maximale**, sans découpe des chiffres,
  élément visuel dominant. Prévoir un rendu valable pour 00:00, 11:11 et 23:59.
- Bandeau supérieur discret : secteur/batterie, date, engrenage.
- Deux lignes compactes Alarme 1 et Alarme 2 : heure et bascule ON/OFF
  directement accessibles. Toucher l'heure ouvre le réglage correspondant.
- Bandeau inférieur : luminosité (soleil, −, niveau 1–10, +), bouton Radio
  (lecture directe), volume (haut-parleur, −, niveau 1–10, +).
- Pendant la sonnerie : grand bouton Snooze, bouton Arrêt.
- Pas de tuiles domotiques, de navigation Axellum ni de popup Axellum en
  tant qu'accueil.

## Exigences fonctionnelles

- Réutiliser les entités et le moteur d'alarme existants sans régression.
- Garder une horloge qui fonctionne même hors ligne après synchronisation.
- Radio : vérifier réellement la lecture audio, ne pas présenter comme
  fonctionnelle une simple commande visuelle.
- Luminosité : agir sur le rétroéclairage physique, pas uniquement sur un nombre.
- Volume : agir sur la sortie audio réelle.
- Aucune dépendance à Home Assistant pour l'affichage et l'activation locale
  des deux alarmes.
- Configuration Wi-Fi et Home Assistant après validation de l'interface.

## Validation avant nouveau flash

1. Compiler ST7121 et exécuter les tests automatiques.
2. Contrôler le rendu de l'heure géante et la lisibilité des commandes.
3. Vérifier les actions des commandes et les transitions d'alarme.
4. Produire un nouveau fichier `.factory.bin` explicitement identifié.
5. Ne pas distribuer le firmware comme terminé sans essais matériels des
   fonctions radio et rétroéclairage.
