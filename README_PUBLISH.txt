RC STUDIO WEBSITE — READY TO PUBLISH

FILES
- index.html: the website and browser DAW
- manifest.webmanifest: installable web app metadata
- sw.js: offline cache for the website shell
- icon.svg: app icon

PUBLISH FREE WITH GITHUB PAGES (PHONE OR COMPUTER)
1. Sign in at https://github.com
2. Create a PUBLIC repository named rc-studio.
3. Upload all four files from this folder into the repository root.
4. Open repository Settings → Pages.
5. Under Build and deployment, choose Deploy from a branch.
6. Select branch main and folder /(root), then Save.
7. Wait for GitHub Pages to publish. Your address will be:
   https://YOUR-GITHUB-USERNAME.github.io/rc-studio/

TO USE
- Open the published address in Chrome.
- Tap PLAY to start the sequencer (audio begins after a user tap).
- Switch BEAT / KEYS / MIXER.
- SAVE downloads a .rcproj JSON project and stores a browser copy.
- LOAD imports a previously saved .rcproj file.
- EXPORT WAV renders the current drum pattern to a WAV file.
- In Chrome, use ⋮ → Add to Home screen / Install app if offered.

LIMITATIONS
This is a static browser DAW starter. Projects are saved locally in the browser or
downloaded as files; there is no cloud account or shared project database.
It does not yet include multitrack audio recording/import, MIDI file support,
server-side accounts, or the complete native C++ engine from the supplied source.
No website has been published by this package; you must upload these files to GitHub
Pages (or another static host) to get a public internet URL.
