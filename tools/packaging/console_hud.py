"""Disc HUD geometry with the compatibility controls required by PC code."""
from pathlib import Path

import kv


RESOURCE = "scripts/hudlayout.res"
VOTE_RESOURCE = "resource/ui/hud/votehud.res"
# Xbox scoreboards offer "A - View Gamer Card"; PC has no gamer cards.
GAMERCARD_RESOURCES = ("resource/ui/scoreboard.res", "resource/ui/survivalmodescoreboard.res")
GAMERCARD_CONTROLS = ("GamerCardButton", "GamerCardLabel")


def hide_gamercard(text):
    """Hide the Xbox Live gamer-card prompt; player selection and kick stay."""
    nodes = kv.parse(text)
    found = set()

    def walk(children):
        for name, value, _ in children:
            if not isinstance(value, list):
                continue
            if name in GAMERCARD_CONTROLS:
                value[:] = [(key, old, cond) for key, old, cond in value
                            if key.lower() not in ("visible", "enabled")]
                value.extend((("visible", "0", None), ("enabled", "0", None)))
                found.add(name)
            else:
                walk(value)

    walk(nodes)
    if found != set(GAMERCARD_CONTROLS):
        raise ValueError("Review scoreboard gamer-card controls")
    return kv.dump(nodes) + "\n"


def patch_votehud(text):
    """Keep native PC vote labels explicitly hidden alongside Xbox prompts.

    The PC VoteStart handler still writes YesPCLabel/NoPCLabel, and live
    testing found both PC and Xbox instructions visible in the same rows.
    Retaining the named PC controls makes their initial visibility explicit;
    the supplied workshop independently documents the same compatibility fix.
    Preserve their stock row geometry for the native per-device selector.
    """
    nodes = kv.parse(text)
    if len(nodes) != 1 or not isinstance(nodes[0][1], list):
        raise ValueError("Review vote HUD root")
    active = [value for name, value, _ in nodes[0][1]
              if name.lower() == "voteactive" and isinstance(value, list)]
    if len(active) != 1:
        raise ValueError("Review active vote panel")
    active = active[0]
    for choice in ("Yes", "No"):
        reference = [value for name, value, _ in active
                     if name == choice + "LeftLabel" and isinstance(value, list)]
        if len(reference) != 1:
            raise ValueError("Review Xbox vote instruction labels")
        # Stock PC labels span the full instruction row; retain runtime
        # English/non-English widths, fonts and colors from the Xbox source.
        replace = {"fieldname", "xpos", "wide", "visible", "enabled",
                   "labeltext", "textalignment"}
        label = [(name, value, cond) for name, value, cond in reference[0]
                 if name.lower() not in replace]
        label.extend([
            ("fieldName", choice + "PCLabel", None),
            ("xpos", "10", None),
            ("wide", "180", "$ENGLISH"),
            ("wide", "260", "!$ENGLISH"),
            ("visible", "0", None),
            # Labels do not accept input. They must remain enabled so the
            # native per-device selector can show full-brightness text.
            ("enabled", "1", None),
            ("labelText", "", None),
            ("textAlignment", "center", None),
        ])
        active[:] = [(name, value, cond) for name, value, cond in active
                     if name.lower() != (choice + "PCLabel").lower()]
        active.append((choice + "PCLabel", label, None))
    # Live German text clips "NEIN abzustimmen" in the disc's equal-width
    # columns. Its left phrase needs only about 75 of 118 logical units.
    # Transfer 20 units to the right column without changing the vote panel,
    # glyph size, font, row height, or the original right edge at x=270.
    for choice in ("Yes", "No"):
        for suffix, key, english, other, german in (
                ("LeftLabel", "wide", "83", "118", "98"),
                ("Icon", "xpos", "87", "128", "108"),
                ("RightLabel", "xpos", "112", "152", "132"),
                ("RightLabel", "wide", "82", "118", "138")):
            children = [value for name, value, _ in active
                        if name == choice + suffix and isinstance(value, list)]
            if len(children) != 1:
                raise ValueError("Review Xbox vote instruction geometry")
            child = children[0]
            original = [(english, "$ENGLISH"), (other, "!$ENGLISH")]
            adjusted = [(english, "$ENGLISH"),
                        (other, "!$ENGLISH && !$GERMAN"), (german, "$GERMAN")]
            current = [(value, cond) for name, value, cond in child
                       if name.lower() == key]
            if current not in (original, adjusted):
                raise ValueError("Review localized Xbox vote instruction geometry")
            child[:] = [(name, value, cond) for name, value, cond in child
                        if name.lower() != key]
            child.extend((key, value, cond) for value, cond in adjusted)
    # Current German failure headings wrap into two DefaultLarge (18-unit)
    # lines. The disc's 17-unit title clips both lines when vertically
    # centered. Keep the font and width; reserve two lines and move the
    # explanation below them, with ten units of bottom padding.
    for panel_name, title_name, detail_name in (
            ("VoteFailed", "FailedTitle", "NotEnoughVotes"),
            ("CallVoteFailed", "FailedTitle", "FailedReason")):
        panels = [value for name, value, _ in nodes[0][1]
                  if name == panel_name and isinstance(value, list)]
        if len(panels) != 1:
            raise ValueError("Review vote result panel")
        panel = panels[0]
        title = [value for name, value, _ in panel
                 if name == title_name and isinstance(value, list)]
        detail = [value for name, value, _ in panel
                  if name == detail_name and isinstance(value, list)]
        if len(title) != 1 or len(detail) != 1:
            raise ValueError("Review vote result title/explanation")
        original_height = ([("67", "$ENGLISH"), ("77", "!$ENGLISH")]
                           if panel_name == "CallVoteFailed" else [("67", None)])
        adjusted_height = ([("67", "$ENGLISH"),
                            ("77", "!$ENGLISH && !$GERMAN"), ("98", "$GERMAN")]
                           if panel_name == "CallVoteFailed"
                           else [("67", "!$GERMAN"), ("98", "$GERMAN")])
        for child, key, original, adjusted in (
                (panel, "tall", original_height, adjusted_height),
                (title[0], "tall", [("17", None)],
                 [("17", "!$GERMAN"), ("36", "$GERMAN")]),
                (detail[0], "ypos", [("29", None)],
                 [("29", "!$GERMAN"), ("48", "$GERMAN")])):
            current = [(value, cond) for name, value, cond in child
                       if name.lower() == key]
            if current not in (original, adjusted):
                raise ValueError("Review localized vote result geometry")
            child[:] = [(name, value, cond) for name, value, cond in child
                        if name.lower() != key]
            child.extend((key, value, cond) for value, cond in adjusted)
    return kv.dump(nodes) + "\n"


def generate(out_dir):
    # The standard Xbox conversion already selected all platform branches.
    # Adapt its current resource in place so newer controls and runtime
    # language/device conditions survive; leave the v27 package untouched.
    target = Path(out_dir) / RESOURCE
    nodes = kv.parse(target.read_text(encoding="utf-8-sig"))
    roots = [value for _, value, _ in nodes if isinstance(value, list)]
    if len(roots) != 1:
        raise ValueError("Review HUD layout root")
    panels = [value for name, value, _ in roots[0]
              if name.lower() == "hudweaponselection" and isinstance(value, list)]
    if len(panels) != 1:
        raise ValueError("Review native inventory panel")
    panel = panels[0]
    heights = [(value, condition) for name, value, condition in panel if name.lower() == "tall"]
    if heights not in ([("200", None)], [("175", None)]):
        raise ValueError("Review native inventory height")
    # Both values come from the actual disc's HudWeaponSelection resource.
    # The adapter retains the live per-player lastinv binding text.
    for key, value in (("tall", "175"), ("PrimaryBindingYPos", "38")):
        panel[:] = [(name, old, cond) for name, old, cond in panel if name.lower() != key.lower()]
        panel.append((key, value, None))
    target.write_text(kv.dump(nodes) + "\n", encoding="utf-8", newline="\n")
    vote = Path(out_dir) / VOTE_RESOURCE
    vote.write_text(patch_votehud(vote.read_text(encoding="utf-8-sig")),
                    encoding="utf-8", newline="\n")
    for relative in GAMERCARD_RESOURCES:
        board = Path(out_dir) / relative
        board.write_text(hide_gamercard(board.read_text(encoding="utf-8-sig")),
                         encoding="utf-8", newline="\n")
    return [RESOURCE, VOTE_RESOURCE, *GAMERCARD_RESOURCES]
