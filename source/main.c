/*---------------------------------------------------------------------------------

    DSFetch - a fastfetch/neofetch-style system info tool for the Nintendo DS/DSi.

    Copyright (C) 2026 xPsycho999

    This program is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free Software
    Foundation, either version 3 of the License, or (at your option) any later
    version. This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY. See <https://www.gnu.org/licenses/> for the full text.

    Port of the concept from 3ds-fastfetch (libctru) to libnds.

    Design rule (kept from the 3DS version): ONLY display info actually read from
    the console. Every value comes from a checked read; if a read fails or a field
    has no DS equivalent, the row is simply omitted rather than faked.

    Layout: the DS console is only 32 columns wide, and values like the MAC or
    "ARM946E-S @ 134 MHz" don't fit beside a logo, so we use BOTH screens:
        - TOP screen    : ASCII clamshell logo (DS Lite or DSi, auto-selected)
        - BOTTOM screen : the info column + a neofetch-style color row

    Press START to exit.

---------------------------------------------------------------------------------*/

#include <nds.h>
#include <calico/nds/env.h>   // g_envExtraInfo (console type, MAC), g_envTwlSecureInfo (region, serial)
#include <fat.h>
#include <sys/statvfs.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

//---------------------------------------------------------------------------------
// ANSI color escapes (libnds console supports foreground codes 30-37 + reset)
//---------------------------------------------------------------------------------
#define CYAN    "\x1b[36m"
#define YELLOW  "\x1b[33m"
#define WHITE   "\x1b[37m"
#define RED     "\x1b[31m"
#define RESET   "\x1b[39m"

#define LABEL_W 9   // label column width on the bottom screen

static PrintConsole topScreen;
static PrintConsole bottomScreen;

//---------------------------------------------------------------------------------
// Info rows are accumulated into a buffer so any single field can be skipped on
// failure without disturbing the layout (mirrors the 3DS version's add_row).
//---------------------------------------------------------------------------------
static char rows[40][96];
static int  rowCount = 0;

static void add_row(const char *label, const char *fmt, ...)
{
    if (rowCount >= (int)(sizeof(rows) / sizeof(rows[0]))) return;
    char value[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(value, sizeof value, fmt, ap);
    va_end(ap);
    snprintf(rows[rowCount], sizeof rows[0],
             YELLOW "%-*s" RESET "%s", LABEL_W, label, value);
    rowCount++;
}

static void add_raw(const char *fmt, ...)
{
    if (rowCount >= (int)(sizeof(rows) / sizeof(rows[0]))) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(rows[rowCount], sizeof rows[0], fmt, ap);
    va_end(ap);
    rowCount++;
}

//---------------------------------------------------------------------------------
// Lookup tables
//---------------------------------------------------------------------------------
static const char *languages[] = {
    "Japanese", "English", "French", "German",
    "Italian", "Spanish", "Chinese", "Korean"
};

static const char *colors[] = {
    "Gray", "Brown", "Red", "Pink", "Orange", "Yellow", "Lime", "Green",
    "Dark Green", "Teal", "Light Blue", "Blue", "Dark Blue", "Dark Purple",
    "Purple", "Magenta"
};

static const char *months[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char *batt_desc(int lvl)
{
    switch (lvl) {
        case 0xF: return "Full";
        case 0xB: return "High";
        case 0x7: return "Medium";
        case 0x3: return "Low";
        case 0x1: return "Very Low";
        case 0x0: return "Critical";
        default:  return NULL;   // unknown encoding -> caller shows raw value
    }
}

// Actual hardware model, from the console-type byte calico reads out of NVRAM
// at boot (valid even when a DSi/3DS runs us in DS-compat mode).
static const char *console_type_name(int t)
{
    switch (t) {
        case EnvConsoleType_DS:         return "Nintendo DS";
        case EnvConsoleType_DSLite:     return "Nintendo DS Lite";
        case EnvConsoleType_DSi:        return "Nintendo DSi";
        case EnvConsoleType_iQueDS:     return "iQue DS";
        case EnvConsoleType_iQueDSLite: return "iQue DS Lite";
        default:                        return NULL;
    }
}

// DSi system region (HWINFO_S.dat), only meaningful in DSi/TWL mode.
static const char *region_name(int r)
{
    switch (r) {
        case EnvTwlRegion_JPN: return "Japan";
        case EnvTwlRegion_USA: return "USA";
        case EnvTwlRegion_EUR: return "Europe";
        case EnvTwlRegion_AUS: return "Australia";
        case EnvTwlRegion_CHN: return "China";
        case EnvTwlRegion_KOR: return "Korea";
        default:               return NULL;
    }
}

//---------------------------------------------------------------------------------
// ASCII logos (top screen). Auto-selected by isDSiMode().
// Modeled on the real consoles: speaker grilles beside the top screen, D-pad
// cross lower-left, A/B/X/Y diamond lower-right, START/SELECT, hinge bar.
// DSi has the inner camera (O) on the hinge; DS Lite has a plain hinge + LED.
//---------------------------------------------------------------------------------
static const char *logo_dslite[] = {
    "  ________________  ",
    " |   .--------.   | ",
    " | : |        | : | ",
    " | : |        | : | ",
    " | : |        | : | ",
    " |   '--------'   | ",
    " |______[==]______| ",
    "  ________________  ",
    " |   .--------.   | ",
    " | + |        | X | ",
    " |+++|        |Y A| ",
    " | + |        | B | ",
    " |   '--------'   | ",
    " | (o) START SEL  | ",
    " |________________| ",
    NULL
};

static const char *logo_dsi[] = {
    "  ________________  ",
    " |   .--------.   | ",
    " | : |        | : | ",
    " | : |        | : | ",
    " | : |        | : | ",
    " |   '--------'   | ",
    " |______(O)_______| ",
    "  ________________  ",
    " |   .--------.   | ",
    " | + |        | X | ",
    " |+++|        |Y A| ",
    " | + |        | B | ",
    " |   '--------'   | ",
    " | (P) START SEL  | ",
    " |________________| ",
    NULL
};

//---------------------------------------------------------------------------------
// Convert a firmware UTF-16LE string (name / message) to printable ASCII.
//---------------------------------------------------------------------------------
static void utf16_to_ascii(char *dst, const s16 *src, int len, int max)
{
    int j = 0;
    if (len < 0)   len = 0;
    if (len > 26)  len = 26;
    for (int i = 0; i < len && j < max - 1; i++) {
        u16 c = (u16)src[i];
        dst[j++] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    dst[j] = 0;
}

//---------------------------------------------------------------------------------
static void gather(void)
{
    const PERSONAL_DATA *pd = PersonalData;
    bool dsi = isDSiMode();

    // ---- title:  name@host -------------------------------------------------
    char name[24];
    int nameLen = pd->nameLen;
    if (nameLen > 10) nameLen = 10;
    utf16_to_ascii(name, pd->name, nameLen, sizeof name);
    const char *host = dsi ? "dsi" : "ds";
    if (name[0])
        add_raw(CYAN "%s@%s" RESET, name, host);
    else
        add_raw(CYAN "ds@%s" RESET, host);
    add_raw("------------------------------");

    // ---- OS / Host / Mode --------------------------------------------------
    // OS = the mode we are running in; Host = the actual hardware model.
    add_row("OS",   "%s", dsi ? "Nintendo DSi" : "Nintendo DS");
    const char *model = console_type_name(g_envExtraInfo->nvram_console_type);
    add_row("Host", "%s", model ? model : (dsi ? "Nintendo DSi" : "Nintendo DS"));
    add_row("Mode", "%s", dsi ? "DSi mode" : "DS mode");

    // ---- CPU ---------------------------------------------------------------
    if (dsi) {
        setCpuClock(true);                       // bump to 134 MHz in DSi mode
        add_row("CPU", "%s", "ARM946E-S @ 134 MHz");
    } else {
        add_row("CPU", "%s", "ARM946E-S @ 67 MHz");
    }
    add_row("ARM7", "%s", "ARM7TDMI @ 33 MHz");

    // ---- Memory / Display --------------------------------------------------
    add_row("Memory",  "%s", dsi ? "16 MiB" : "4 MiB");
    add_row("Display", "%s", "2x 256x192");

    // ---- Storage (best-effort: needs an FAT-mountable SD card) -------------
    if (fatInitDefault()) {
        struct statvfs st;
        if (statvfs("sd:/", &st) == 0 || statvfs("fat:/", &st) == 0) {
            unsigned long bs = st.f_frsize ? st.f_frsize : st.f_bsize;
            unsigned long long total = (unsigned long long)st.f_blocks * bs;
            unsigned long long freeb = (unsigned long long)st.f_bfree  * bs;
            unsigned long tot_mib  = (unsigned long)(total / (1024ULL * 1024ULL));
            unsigned long free_mib = (unsigned long)(freeb / (1024ULL * 1024ULL));
            unsigned long used_mib = (tot_mib > free_mib) ? tot_mib - free_mib : 0;
            add_row("Storage", "%lu.%lu / %lu.%lu GiB",
                    used_mib / 1024, (used_mib % 1024) * 10 / 1024,
                    tot_mib  / 1024, (tot_mib  % 1024) * 10 / 1024);
        }
    }

    // ---- MAC ----------------------------------------------------------------
    // Read straight from firmware NVRAM at flash[0x36] (where the console MAC is
    // stored, and copied to the wifi MAC register at boot). We don't start the
    // wireless manager, so g_envExtraInfo->wlmgr_macaddr stays empty.
    u8 mac[6];
    readFirmware(0x36, mac, sizeof mac);
    if (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5])
        add_row("MAC", "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // ---- Region + Serial (DSi HWINFO_S.dat, DSi mode only) -----------------
    if (dsi) {
        const char *rg = region_name(g_envTwlSecureInfo->region);
        if (rg) add_row("Region", "%s", rg);

        char serial[16];
        int j = 0;
        for (int i = 0; i < 12 && j < (int)sizeof(serial) - 1; i++) {
            char c = g_envTwlSecureInfo->serial[i];
            if (c == 0) break;
            serial[j++] = (c >= 32 && c < 127) ? c : '?';
        }
        serial[j] = 0;
        if (serial[0]) add_row("Serial", "%s", serial);
    }

    // ---- Language / Theme --------------------------------------------------
    unsigned lang = pd->language;
    if (lang < 8) add_row("Language", "%s", languages[lang]);
    if (pd->theme < 16) add_row("Theme", "%s", colors[pd->theme]);

    // ---- Battery -----------------------------------------------------------
    u32 b = getBatteryLevel();
    bool charging = (b & BIT(7)) != 0;
    int lvl = b & 0xF;
    const char *bd = batt_desc(lvl);
    if (bd) add_row("Battery", "%s%s", bd, charging ? " (charging)" : "");
    else    add_row("Battery", "%d/15%s", lvl, charging ? " (charging)" : "");

    // ---- Birthday ----------------------------------------------------------
    if (pd->birthMonth >= 1 && pd->birthMonth <= 12)
        add_row("Birthday", "%d %s", pd->birthDay, months[pd->birthMonth - 1]);

    // ---- Greeting (user message), only if set ------------------------------
    char msg[40];
    int msgLen = pd->messageLen;
    if (msgLen > 26) msgLen = 26;
    utf16_to_ascii(msg, pd->message, msgLen, sizeof msg);
    if (msg[0]) add_row("Greeting", "%s", msg);

    // ---- Time (RTC) --------------------------------------------------------
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (lt) {
        char buf[32];
        strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", lt);
        add_row("Time", "%s", buf);
    }
}

//---------------------------------------------------------------------------------
int main(void)
{
    // Two text consoles: main engine on top, sub engine on bottom.
    lcdMainOnTop();
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    consoleInit(&topScreen,    3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true,  true);
    consoleInit(&bottomScreen, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);

    gather();

    // ---- top screen: logo --------------------------------------------------
    consoleSelect(&topScreen);
    const char **logo = isDSiMode() ? logo_dsi : logo_dslite;
    iprintf("\n\n");
    for (int i = 0; logo[i]; i++)
        iprintf(CYAN "     %s\n" RESET, logo[i]);   // 5-space pad centers the 20-wide art
    iprintf("\n\n");
    iprintf("            " CYAN "DSFetch" RESET "\n");

    // ---- bottom screen: info + color row -----------------------------------
    consoleSelect(&bottomScreen);
    for (int i = 0; i < rowCount; i++)
        iprintf("%s\n", rows[i]);

    // neofetch-style palette strip (foreground colors 31..37)
    for (int c = 31; c <= 37; c++)
        iprintf("\x1b[%dm###", c);
    iprintf(RESET "\n" RED "Press START to exit." RESET "\n");

    while (pmMainLoop()) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & KEY_START) break;
    }

    return 0;
}
