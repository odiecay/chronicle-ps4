# Installing Chronicle PS4 (Dark Cloud)

You need:

* a PS4 jailbroken with GoldHEN (tested on firmware 13.52),
* your own Dark Cloud disc image as an `.iso` file (about 1.7 GB). **The USA / NTSC 1.02 disc is the version tested on the console.** The port also knows the PAL release, but that has not been tried on a PS4 yet,
* a USB drive formatted exFAT or FAT32 (or an FTP client), and about 4 GB free on the PS4.

The package is the port, not the game. The first time you start it, a setup screen copies the game's files out of your own disc image. You only do this once.

## The easy way: a USB drive

1. On the drive, make a folder called **`darkcloud`** and put your `.iso` in it. Put `DarkCloud-PS4-01.00.pkg` in the drive's main folder.
2. Plug the drive into the PS4.
3. Open GoldHEN's Package Installer (Settings > GoldHEN > Package Installer, or Debug Settings > Game > Package Installer) and install the package.
4. Start **Chronicle PS4 (Dark Cloud)** from the home screen and leave the drive plugged in.

From here it looks after itself. The game finds your disc image, tells you what it is about to do, and starts on its own after a few seconds (press **X** to start straight away). It copies the disc image across from the drive, unpacks the game from it, and tidies up after itself. A few minutes later it says the game is ready. Press **X** to play.

## Using FTP instead

1. Install the package as above and start the game once.
2. Turn on GoldHEN's FTP server (port 2121, no user name or password) and set your FTP program to **binary** mode.
3. Copy the `.iso` into `/data/darkcloud/` (lower case). The setup screen sees it and carries on in the same way.

## What the setup does, in plain words

* The game can't read a USB drive on its own. So it asks GoldHEN's FTP server, which runs on the PS4 itself, to hand the disc image across. Nothing goes over your network.
* It copies the disc image into the PS4, unpacks the game's files from it into `/data/darkcloud/data`, then deletes the copy. Your original disc image is never changed.
* GoldHEN's FTP server is on by default. If the game says it can't reach it, switch it on under Settings > GoldHEN > Server Settings.
* The screen also fills the waiting time with a few facts about Dark Cloud.

## If something goes wrong

* **If setup stops or seems stuck for any reason:** close the game, delete the `/data/darkcloud/data` folder (and `/data/darkcloud/data.partial` and `/data/darkcloud/setup-copy.part` if they are there), keep your disc image in place, and start the game again. Setup begins again from the start.
* **"This does not look like a Dark Cloud disc image":** the file is damaged, from a different game, or was copied in ASCII mode (set your FTP program to binary and copy it again).
* **A `.zip`, `.7z` or `.rar` in the folder:** unpack the `.iso` from it first.
* **Not enough space:** setup needs about 4 GB free for the USB route (the copy plus the unpacked game), about 2 GB if the `.iso` is already on the PS4.
* **The game says it can't reach the FTP server:** GoldHEN's FTP server is on by default. If it has been switched off, turn it on under Settings > GoldHEN > Server Settings, or copy the `.iso` to `/data/darkcloud/` yourself.
* **To run setup again** (for example to use a different disc): delete the `/data/darkcloud/data` folder, then start the game.

## Notes

* Saves are in `/data/darkcloud/save`. The log is `/data/darkcloud/ps4_boot.log`: attach it to bug reports.
* Start the game from the home screen. Starting it through a remote-control service can crash the system UI.
* Updating the package later keeps your game files and saves.
* Optional switches are empty files in `/data/darkcloud/`; see `ps4/README.md`.
