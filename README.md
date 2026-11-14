# ps5-unhide-debug-settings

Payloads PS5 (ps5-payload-sdk) qui modifient l'affichage des icônes de l'écran d'accueil via
`/system_data/priv/mms/app.db` (tables `tbl_iconinfo*`). Avant chaque modification,
l'`app.db` est sauvegardé dans `/data/app.db.<timestamp>.bak`.

| Payload | Effet |
|---|---|
| `welcome_hub.elf` | met `visible=1` pour `NPXS40063` (Welcome Hub) |
| `toggle_store.elf` | **bascule** l'icône PlayStation Store (`NPXS40047`) : la cache si elle est visible, la réaffiche si elle est cachée |

Redémarre la console après l'exécution. Ne les utilise que sur ta propre console.
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
