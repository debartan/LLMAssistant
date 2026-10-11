# Icons

One text file per app, 24 columns by 24 rows. `#` is a lit pixel, anything
else is dark. `tools/gen_icons.py` turns every file here into a 96 by 96
LVGL alpha bitmap (each art pixel becomes a 4 by 4 block) and writes
`main/assets/icons.c` and `icons.h`. The symbol name is
`icon_<file name without extension>`.

Run it from the `firmware` directory after adding or editing an icon:

    python3 tools/gen_icons.py
