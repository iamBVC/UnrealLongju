# Localization Workflow

English (`en`) is the source culture. Translations live in standard GNU PO files under
`Content/Localization/Game/<culture>/Game.po`. This workflow is offline by default: no game
text, credentials, or source code is uploaded to a translation provider.

## Add A Language

Use an IETF culture code:

```powershell
.\AddLocalizationCulture.ps1 de
.\AddLocalizationCulture.ps1 fr
.\AddLocalizationCulture.ps1 pt-BR
```

The script adds the culture to gathering, importing, packaging, and the in-game selector.
Do not expose a language in a release until its translation has passed review.

## Translation Cycle

1. Run `Scripts\GatherLocalization.bat` after changing source text or assets.
2. Run `Scripts\ExportLocalization.bat` to update every PO file.
3. Translate `Content/Localization/Game/<culture>/Game.po` with an offline CAT editor such as
   Poedit, memoQ, or Trados. Preserve `msgctxt`, placeholders such as `{0}` and `%s`, markup,
   line breaks, and terminology.
4. Run `Scripts\ValidateLocalization.bat -Cultures it` while translating.
5. Run `Scripts\ValidateLocalization.bat -Strict` for a release. Strict mode rejects missing strings;
   all modes reject placeholder mismatches.
6. Run `Scripts\ImportLocalization.bat` to compile PO files into `.locres` resources.
7. Test the language in Editor and a packaged Client.

## Professional Review

- Keep PO files in Git and review translation changes like code.
- Give translators the PO files, not the game source or database.
- Maintain a shared terminology glossary for names, empires, skills, stats, and item types.
- Use one human translator and one independent reviewer per language.
- Test variable expansion, clipping, fonts, plural forms, and right-to-left layout where needed.
- Never place passwords, tokens, player data, or internal server messages in localizable text.

Machine translation can produce a first draft, but should run through an approved provider or a
self-hosted service and must still pass human review. API keys belong in environment variables or
a secret manager, never in this repository.
