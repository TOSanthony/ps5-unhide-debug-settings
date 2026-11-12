# ps5-unhide-debug-settings

Payload PS5 (ps5-payload-sdk) qui :

1. sauvegarde `/system_data/priv/mms/app.db` dans `/data/app.db.<timestamp>.bak` ;
2. met `visible=1` pour `titleId = NPXS40063` dans les tables `tbl_iconinfo*`.

Redémarre la console après l'exécution. Ne l'utilise que sur ta propre console.

## Build via GitHub Actions

- Chaque push sur `main` compile `unhide_debug.elf` : récupère-le dans l'onglet **Actions → build → Artifacts**.
- Pour publier une release : `git tag v1.0.0 && git push origin v1.0.0` (l'ELF est joint à la release).

## Build local

```bash
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
# placer sqlite3.c et sqlite3.h (amalgamation SQLite) dans ce dossier
make
```
