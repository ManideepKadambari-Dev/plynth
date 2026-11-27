# Setting up Plynth

Plynth needs a few things on your computer before it can import models: Blender, a library folder and a projects folder. The setup check looks at all of them and tells you what is missing and what to do.

The setup check only looks, with one exception. It downloads Blender when you press **Get Blender for me** and answer Download. It never installs or downloads anything else.

## Where to find it

- **Help > Setup check** opens it at any time.
- It opens by itself the first time you start Plynth, after Plynth is updated, and whenever a required item is missing.
- From a terminal, `plynth doctor` runs the same check and prints the result as JSON. See the [command line guide](CLI_GUIDE.md).

## The screen

One row for each item. A row shows the name, a state word, the version and place that was found, a sentence about what was found and a sentence about what to do. The summary at the top says "Ready" or "2 items need you".

Press **Check again** after you change something outside Plynth, for example after installing Blender. Press **Done** to close the screen. When everything required is found, it does not open by itself again until Plynth is updated.

### What the state words mean

| State | Meaning |
|---|---|
| Found | The item is there and good. |
| Missing | The item is not there. If it is required, Plynth cannot import models until you fix it. |
| Too old | It is there but older than Plynth can use. Required items in this state block too. |
| Needs attention | It works, but look at it: an untested version, low disk space, or an add-on that is installed but not signed in. It never blocks. |
| Not checked | Plynth does not check this yet, or you asked it not to. It never blocks. |

## The items

### Blender

Plynth uses Blender in the background to clean up the models you import, to draw their thumbnails and to build your building. You never see its window.

Plynth looks for Blender in the places Windows normally puts it: the Program Files folder, your local programs folder, the list of installed programs, the Microsoft Store, Steam, and your PATH. If it finds several, it picks the newest supported one and lists all of them in a drop-down so you can pick another.

If Blender is missing, you have two ways to get one.

**Let Plynth get it.** Press **Get Blender for me**. Plynth asks blender.org for the newest Blender in the series it was developed with and shows the version, the size (about 400 MB) and the server. Press **Download** to go on, or **Not now** to close the box. A progress bar shows the download and **Stop** cancels it. When the file is complete, Plynth checks it against the SHA-256 checksum that blender.org publishes. If it does not match, nothing is kept. Plynth then unpacks the official portable Blender into the `portable-blender` folder inside the Plynth folder, selects it and checks again. If a step fails, the box says which one and offers **Try again**. The installer has the same choice, a tick box named **Get Blender for me**, ticked by default and skipped when a usable Blender is already found. From a terminal, `plynth blender get` does the same, and `--yes` skips the question. See the [command line guide](CLI_GUIDE.md). Uninstalling Plynth removes this Blender with the Plynth folder.

**Choose your own.** Press **Get Blender** to open blender.org in your browser, install Blender yourself, then press **Check again**.

If Blender is installed somewhere unusual, press **Browse** and pick its `blender.exe`. To use your own Blender instead of the downloaded one, open the drop-down or press **Browse** and pick it.

When Plynth has to choose a Blender, it uses the first of these that is set: the `--blender` option of a command, the `PLYNTH_BLENDER` variable, the Blender saved in the settings, and then the search above. A Blender named by the option or the variable is the only one Plynth starts. If that one is missing or too old, the check says so. It does not quietly use another install.

### Blender version

Plynth needs Blender 4.2 or newer. It was developed with Blender 5.2 LTS.

- Older than 4.2: the row says Too old and importing is blocked. Install a newer Blender next to the old one. You do not need to remove the old one. Plynth picks the newer one.
- A newer major version than Plynth was tested with: the row says Needs attention and importing is allowed. If something goes wrong with a model, try the version Plynth was developed with.

### Library folder

The library is one folder on your computer that holds your assets catalog, and later your materials and your own opening shapes. It is shared by every project. Put it somewhere with room to grow: models and textures can be large. Below 2 GB of free space the row says Needs attention.

When you first run Plynth, it suggests `Documents\Plynth\Library`. To choose the folder:

- **Choose folder** lets you pick any folder. If it is not a library yet, the row says Missing and shows **Create**.
- **Create** makes the folder into a library: it creates the folders inside and the catalog databases. It never removes or changes anything already in the folder, so it is safe to choose an existing library or a folder with other files.

Inside the library:

```
Library/
  assets/
    assets.db        the search index
    items/           one folder for each asset
    inbox/           drop model files here to import them
  materials/         the materials catalog, laid out like assets/
  generators/        generator scripts (.py), see Writing generators
  integrations.json  the options of BlenderKit and Fab, made when you change one
  shapes/            your own opening shapes
```

The files in `items/` are the real catalog. `assets.db` is only an index. If you lose it, **Catalog > Rescan library** builds it again from the folders. What does not come back is your own list of room types, which is kept only in the index. The built-in room types and the categories your assets use do come back.

The folder must be writable. If it is read only or on a drive that is not connected, the row says Missing and tells you so.

### Projects folder

New projects are saved here, and the built-in terminal starts here when no project is open. When you first run Plynth, it suggests `Documents\Plynth\Projects`. The folder must exist and be writable. **Create** makes it. **Choose folder** picks another one.

### Graphics driver

The app checks when it starts that your graphics card can run the 3D view. The row says Found with the name of the card and the graphics interface it uses (Direct3D 11). If it says Missing, the 3D view cannot draw. Update your graphics driver, restart Plynth and check again. The row is required, so while it is Missing the Build button opens the setup check instead of building. See "If the 3D view stays empty" in the [User guide](USER_GUIDE.md).

`plynth doctor` has no window to draw in, so it says Not checked for this row. That never blocks the command line, and `plynth build` still runs.

### Add-ons

Add-ons are optional extras that live inside Blender. Plynth never needs them to import your own models, and it never installs them. The setup check finds out which add-ons are switched on and shows what stays off without each one. It does this by reading your saved Blender preferences (`userpref.blend`) and the add-on folders as plain data. When BlenderKit is on and the preferences hold no key, it may also read BlenderKit's `config\preferences.json`, only to see whether a sign in exists. It does not open your add-ons or change anything in your Blender folders.

| Add-on | What it is for |
|---|---|
| BlenderKit | Importing models and materials from BlenderKit |
| Archimesh | The Archimesh Column sample generator, and other generators that use it |
| Home Builder | The Base Cabinets sample generator |
| Bool Tool | The Slotted Screen sample generator |
| Asset Optimizer | Generators that tidy the meshes they make |
| Bagapie | The Post and Rail Fence sample generator |

Each add-on row says Found or Missing. Some have an **Open page** button that opens the official page where you can get the add-on. Install add-ons from inside Blender (Edit, Preferences, Get Extensions or Add-ons).

**BlenderKit and signing in.** BlenderKit needs your own BlenderKit account. Install and enable the add-on in Blender, then open Edit, Preferences, Add-ons, BlenderKit and sign in. Plynth only checks whether a sign in is stored. It never shows, logs or copies your key or password. If the add-on is on but you are not signed in, the row says Needs attention.

**BlenderKit downloads use your own Blender settings.** The add-on check runs none of your add-ons, but a BlenderKit download does. It starts Blender in the background, without a window, with your own Blender settings and the add-ons you have enabled. Blender and those add-ons can change files in your Blender folders during that run. Plynth does not start Blender with a clean setup for this, because BlenderKit needs your add-on and your sign in. Plynth shows this reminder next to BlenderKit in the app. There is no option to show a Blender window.

When your preferences hold no key, a download reads the saved BlenderKit settings file to see that it is valid before it lets the add-on load it. If that file is damaged, Plynth leaves it as it is and does not repair or delete it. It tells you to sign in again inside Blender (Edit, Preferences, Add-ons, BlenderKit).

Checking add-ons starts Blender once with its factory settings, which takes a few seconds. The setup check, `plynth doctor` and `plynth library integrations` do it, and so does the Sources window when you first open it or press Check again. `--no-addons` skips it in both commands. If your preferences file cannot be understood, the add-on rows say Not checked.

## The settings file

Your choices are saved in one file for your Windows account:

```
%APPDATA%\Plynth\settings.json
```

`%APPDATA%` is the Roaming folder of your Windows account. Type `%APPDATA%\Plynth` in the Explorer address bar to open it. The file holds the library folder, the projects folder, the Blender you chose, whether to keep imported zips, the triangle warning limit and the Plynth version whose setup check last passed. You can read it in any text editor. The usual way to change it is the setup check or `plynth settings set`.

Settings belong to the computer, not to a project, so they are not saved in your project folders.

To use a different settings file, set the environment variable `PLYNTH_CONFIG_DIR` to a folder. Plynth then reads and writes `settings.json` in that folder. Scripts use this to stay away from your real settings.

## plynth doctor

`plynth doctor` is the setup check for the command line. It prints one JSON answer with every item, its state and what to do. It exits with 0 even when something is missing, so a script reads the `ready` value instead.

```
plynth doctor
```

Useful options:

- `--no-addons` skips the add-on check, so the add-on probe is not run and your preferences are not read. Blender is still started briefly with `--version` to read its version.
- `--blender <file>` checks one particular `blender.exe` and no other. It beats the `PLYNTH_BLENDER` variable and the saved setting.

The full answer is described in the [command line guide](CLI_GUIDE.md).

## If something does not work

- **The row says Missing for Blender but you installed it.** Press Check again. If it is still missing, press Browse and pick `blender.exe` by hand.
- **The library row says "not a library".** The folder you chose has no catalog yet. Press Create.
- **Add-ons say Not checked.** The row says why. If Blender could not be started to ask, fix the Blender rows first. If your saved preferences could not be read, Plynth leaves them as they are.
- **A new Blender was installed and Plynth still uses the old one.** Open the Blender drop-down and pick the new one.
