# ps5-unhide-debug-settings

Payloads PS5 (ps5-payload-sdk) qui modifient l'affichage des icônes de l'écran d'accueil via
`/system_data/priv/mms/app.db` (tables `tbl_iconinfo*`). Avant chaque modification,
l'`app.db` est sauvegardé dans `/data/app.db.<timestamp>.bak`.

| Payload | Effet |
|---|---|
| `dashboard.elf` | **tableau de bord web** en cartes (port 8096), avec interrupteurs d'affichage |

### Tableau de bord `dashboard.elf`

Page web en cartes (horloge, stockage de la console, météo, état des tuiles de l'accueil, état d'Orbit Store, raccourcis), à ouvrir depuis un téléphone ou un PC : `http://<IP de la PS5>:8096/`. Des interrupteurs sur la page affichent/masquent le Store, le Welcome Hub et PlayStation Plus, et épinglent/désépinglent Orbit Store (sauvegarde de `app.db` une fois par lancement ; redémarre la PS5 pour appliquer). Activer le Welcome Hub déploie aussi automatiquement le widget `hub-signin-patch` (voir plus bas) ; le désactiver le retire. Pas de batterie des manettes ni de trophées pour l'instant. Raccourcis personnalisés : `/data/dashboard_links.txt` (une ligne `Nom|http://...`). Météo : `/data/dashboard_weather.txt` (`latitude,longitude,Ville`, Paris par défaut ; la météo est récupérée par le navigateur via Open-Meteo). Arrêt : `POST /api/quit` avec l'en-tête `X-Menu: 1`. Ce n'est pas une tuile native de l'écran d'accueil : la page s'ouvre dans un navigateur.

**Note :** `dashboard.elf` épingle/verrouille Orbit Store mais ne maintient plus le verrou en continu (ce rôle était tenu par `icon_menu.elf`, retiré du dépôt). Si un jeu lancé ensuite repasse devant Orbit Store dans la liste, relance simplement l'interrupteur « épingler » depuis le tableau de bord pour reverrouiller.

**Payload auto-embarqué (hors dépôt) :** `dashboard.elf` peut aussi lancer automatiquement, au démarrage, un second payload qui lui est collé à la fin du fichier (format `[ELF additionnel][magic "DASHEMB1"][longueur 8 octets LE]`, lu via `/proc/curproc/file`). Ce mécanisme sert à embarquer un binaire tiers (ex. NiDUS) sans jamais committer son contenu sur GitHub : la fusion se fait uniquement en local, jamais dans ce dépôt.

Redémarre la console après l'exécution. Ne l'utilise que sur ta propre console.
Code commun (sauvegarde, accès SQLite) : `appdb.h`.

## Build via GitHub Actions

- Chaque push sur `main` compile les ELF : onglet **Actions → build → Artifacts**, ou release `latest`.
- Release versionnée : `git tag v1.0.0 && git push origin v1.0.0`.

## Build local

```bash
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
# placer sqlite3.c et sqlite3.h (amalgamation SQLite) dans ce dossier
make
```

### Widget NiDUS `nidus-widgets/hub-signin-patch`

Widget pour [NiDUS](https://github.com/manchildtechnologies-ui/NiDUS) qui force `VshNMPsnAccountModule.IsSignedIn`/`GetState` à répondre « connecté » côté Welcome Hub, pour débloquer l'écran « Se connecter » sans mise à jour du firmware (purement local, aucune connexion réseau/PSN réelle). Son code source (`config.json`, `widget.js`) est embarqué dans `dashboard.elf` via `dashboard/hub_patch_files.h` (généré par `dashboard/gen_hub_patch.py`) et déployé/retiré automatiquement par l'interrupteur Welcome Hub du tableau de bord — pas besoin de le copier à la main.
