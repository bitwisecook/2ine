#!/usr/bin/env python3
# Public domain. No rights reserved. Wholly AI-authored.
"""Native libadwaita sound-bank library and channel mapping editor."""

import copy

import gi

from .views import FormDialog, action_button, empty

try:
    gi.require_version("Gtk", "4.0")
    gi.require_version("Adw", "1")
    from gi.repository import Adw, Gio, GLib, Gtk
except (ImportError, ValueError) as error:
    raise SystemExit("GTK 4 and libadwaita are required: " + str(error)) from error

MIDI_CHANNEL_COUNT = 16
# Program 0 follows the MIDI stream; 1-128 pin General MIDI programs 0-127.
PROGRAM_CHOICE_MAX = 128
# Bank select is a 14-bit MSB/LSB pair; -1 follows the MIDI stream.
MIDI_BANK_MAX = 16383


class AudioDialog(FormDialog):
    def __init__(self, window):
        super().__init__(window, "Sound Banks and MIDI", width=760, height=720)
        self.data = None
        self.channels = []
        self.present(window)
        self.run(lambda: self.api("audio-snapshot"), self.loaded)

    def loaded(self, data):
        self.data = data
        empty(self.body)
        page = self.page()
        settings = data["settings"]
        self.bank_ids = [bank["id"] for bank in data["banks"]]
        bank_names = [bank["name"] for bank in data["banks"]]
        self.profile_ids = [profile["id"] for profile in data["profiles"]]
        group = self.group(
            "Sound Banks",
            "FluidR3 GM by Frank Wen is included. Import SF2, SF3, DLS, SFZ or a ZIP containing one bank. "
            "SFZ imports the complete containing folder, including samples and license files. "
            "Changes apply to the next launch.",
        )
        page.add(group)
        self.bank = Adw.ComboRow(title="Default Bank", model=Gtk.StringList.new(bank_names))
        self.bank.set_selected(self.bank_ids.index(settings["bank"]))
        group.add(self.bank)
        import_row = Adw.ActionRow(title="Install Sound Bank", subtitle="Original files are kept.")
        import_row.add_suffix(action_button("Import…", self.choose))
        group.add(import_row)
        for bank in data["banks"]:
            group.add(
                Adw.ActionRow(
                    title=bank["name"],
                    subtitle=bank["format"].upper() + " · " + bank.get("license", "User supplied"),
                    use_markup=False,
                )
            )
        group = self.group(
            "MIDI Mapping", "Route each incoming channel or apply the selected preset's defaults."
        )
        page.add(group)
        guide = Adw.ExpanderRow(
            title="Mapping Guide", subtitle="Program numbering and instrument compatibility"
        )
        guide.add_row(
            Adw.ActionRow(
                title="Programs and Banks",
                subtitle="Program 0 follows MIDI; 1–128 pins an instrument. Bank −1 and Follow Profile inherit the mapping. GS/XG use available variations with a GM fallback. SFZ defines its own instruments; bank/program/type controls apply to SF2, SF3 and DLS.",
                use_markup=False,
            )
        )
        guide.add_row(
            Adw.ActionRow(
                title="MT-32 Compatibility",
                subtitle="The MT-32 preset maps channels only. Authentic MT-32 synthesis needs Munt and your ROMs.",
                use_markup=False,
            )
        )
        group.add(guide)
        self.profile = Adw.ComboRow(
            title="Mapping Preset",
            model=Gtk.StringList.new([profile["name"] for profile in data["profiles"]]),
        )
        self.profile.set_selected(self.profile_ids.index(settings["profile"]))
        group.add(self.profile)
        reset = Adw.ActionRow(
            title="Apply Mapping Defaults", subtitle="Resets this draft's 16 channels."
        )
        reset.add_suffix(action_button("Apply Defaults", self.defaults))
        group.add(reset)
        self.channels = []
        for index, saved in enumerate(settings["channels"]):
            row = Adw.ExpanderRow(
                title=f"Channel {index + 1}", subtitle="Bank, instrument and output channel"
            )
            target = Adw.ComboRow(
                title="Output Channel",
                model=Gtk.StringList.new([str(i) for i in range(1, MIDI_CHANNEL_COUNT + 1)]),
            )
            target.set_selected(saved["target"] - 1)
            row.add_row(target)
            bank = Adw.ComboRow(
                title="Sound Bank", model=Gtk.StringList.new(["Default Bank", *bank_names])
            )
            bank.set_selected(self.bank_ids.index(saved["bank"]) + 1 if saved["bank"] else 0)
            row.add_row(bank)
            program = Adw.SpinRow.new_with_range(0, PROGRAM_CHOICE_MAX, 1)
            program.set_title("Program (0 Follows MIDI)")
            program.set_value(saved["program"] + 1)
            row.add_row(program)
            number = Adw.SpinRow.new_with_range(-1, MIDI_BANK_MAX, 1)
            number.set_title("Bank Number (−1 Follows MIDI)")
            number.set_value(saved["bankNumber"])
            row.add_row(number)
            percussion = Adw.ComboRow(
                title="Channel Type",
                model=Gtk.StringList.new(["Follow Profile", "Melodic", "Percussion"]),
            )
            percussion.set_selected(saved["percussion"] + 1)
            row.add_row(percussion)
            muted = Adw.SwitchRow(title="Mute")
            muted.set_active(saved["muted"])
            row.add_row(muted)
            self.channels.append((target, bank, program, number, percussion, muted))
            bank.connect("notify::selected", lambda *_: self.update_controls())
            group.add(row)
        self.bank.connect("notify::selected", lambda *_: self.update_controls())
        self.update_controls()

    def update_controls(self):
        for _target, bank, program, number, percussion, _muted in self.channels:
            selected = bank.get_selected()
            index = selected - 1 if selected else self.bank.get_selected()
            enabled = self.data["banks"][index]["format"] != "sfz"
            for control in (program, number, percussion):
                control.set_sensitive(enabled)

    def settings(self):
        settings = copy.deepcopy(self.data["settings"])
        settings["profile"] = self.profile_ids[self.profile.get_selected()]
        settings["bank"] = self.bank_ids[self.bank.get_selected()]
        for saved, controls in zip(settings["channels"], self.channels, strict=True):
            target, bank, program, number, percussion, muted = controls
            selected = bank.get_selected()
            saved.update(
                target=target.get_selected() + 1,
                bank=self.bank_ids[selected - 1] if selected else "",
                program=int(program.get_value()) - 1,
                bankNumber=int(number.get_value()),
                percussion=percussion.get_selected() - 1,
                muted=muted.get_active(),
            )
        return settings

    def defaults(self):
        defaults = self.data["profiles"][self.profile.get_selected()]["channels"]
        for values, controls in zip(defaults, self.channels, strict=True):
            target, bank, program, number, percussion, muted = controls
            target.set_selected(values["target"] - 1)
            bank.set_selected(0)
            program.set_value(values["program"] + 1)
            number.set_value(values["bankNumber"])
            percussion.set_selected(values["percussion"] + 1)
            muted.set_active(values["muted"])

    def choose(self):
        file_filter = Gtk.FileFilter(name="Sound Banks (SF2, SF3, SFZ, DLS, ZIP)")
        for suffix in ("sf2", "sf3", "sfz", "dls", "zip"):
            file_filter.add_suffix(suffix)
        filters = Gio.ListStore.new(Gtk.FileFilter)
        filters.append(file_filter)
        dialog = Gtk.FileDialog(title="Import Sound Bank", filters=filters)

        def chosen(dialog, result):
            try:
                file = dialog.open_finish(result)
            except GLib.Error as error:
                if not error.matches(Gtk.DialogError.quark(), Gtk.DialogError.DISMISSED):
                    self.error_message(error)
                return
            path = file.get_path()
            if not path:
                self.error_message("Choose a local sound-bank file.")
                return
            self.import_file(path)

        dialog.open(self.window, None, chosen)

    def import_file(self, path):
        draft = self.settings()

        def installed(data):
            data["settings"] = draft
            self.loaded(data)

        self.run(lambda: self.api("import-soundfont", path=str(path)), installed)

    def submit(self):
        settings = self.settings()

        def saved(_result):
            self.window.refresh(self.ball)
            self.close()

        self.run(lambda: self.api("save-audio", settings=settings), saved)
