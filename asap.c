/*
 * asap.c - ASAP engine
 *
 * Copyright (C) 2005-2006  Piotr Fusik
 *
 * This file is part of ASAP (Another Slight Atari Player),
 * see http://asap.sourceforge.net
 *
 * ASAP is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * ASAP is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ASAP; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "asap.h"
#include "asap_internal.h"
#include "cpu.h"
#include "pokey.h"
#include "pokeysnd.h"

#include "players.h"

#define CMR_BASS_TABLE_OFFSET  0x70f

static const unsigned char cmr_bass_table[] = {
	0x5C, 0x56, 0x50, 0x4D, 0x47, 0x44, 0x41, 0x3E,
	0x38, 0x35, 0x88, 0x7F, 0x79, 0x73, 0x6C, 0x67,
	0x60, 0x5A, 0x55, 0x51, 0x4C, 0x48, 0x43, 0x3F,
	0x3D, 0x39, 0x34, 0x33, 0x30, 0x2D, 0x2A, 0x28,
	0x25, 0x24, 0x21, 0x1F, 0x1E
};

/* main clock in Hz, PAL (FREQ_17_EXACT is for NTSC!) */
#define ASAP_MAIN_CLOCK  1773447U

UBYTE memory[65536 + 2];

int ANTIC_xpos = 0;
int ANTIC_xpos_limit = 0;
int ANTIC_wsync_halt = 0;

/* structures to hold the 9 pokey control bytes */
UBYTE POKEY_AUDF[4 * POKEY_MAXPOKEYS];	/* AUDFx (D200, D202, D204, D206) */
UBYTE POKEY_AUDC[4 * POKEY_MAXPOKEYS];	/* AUDCx (D201, D203, D205, D207) */
UBYTE POKEY_AUDCTL[POKEY_MAXPOKEYS];	/* AUDCTL (D208) */
int POKEY_Base_mult[POKEY_MAXPOKEYS];		/* selects either 64Khz or 15Khz clock mult */

UBYTE POKEY_poly9_lookup[511];
UBYTE POKEY_poly17_lookup[16385];
static ULONG random_scanline_counter;

static unsigned int enable_stereo = 0;

#ifndef SOUND_GAIN /* sound gain can be pre-defined in the configure/Makefile */
#define SOUND_GAIN 4
#endif

static void POKEY_PutByte(UWORD addr, UBYTE byte)
{
#ifdef STEREO_SOUND
	addr &= enable_stereo ? 0x1f : 0x0f;
#else
	addr &= 0x0f;
#endif
	switch (addr) {
	case POKEY_OFFSET_AUDC1:
		POKEY_AUDC[POKEY_CHAN1] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDC1, byte, 0, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDC2:
		POKEY_AUDC[POKEY_CHAN2] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDC2, byte, 0, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDC3:
		POKEY_AUDC[POKEY_CHAN3] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDC3, byte, 0, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDC4:
		POKEY_AUDC[POKEY_CHAN4] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDC4, byte, 0, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDCTL:
		POKEY_AUDCTL[0] = byte;

		/* determine the base multiplier for the 'div by n' calculations */
		if (byte & POKEY_CLOCK_15)
			POKEY_Base_mult[0] = POKEY_DIV_15;
		else
			POKEY_Base_mult[0] = POKEY_DIV_64;

		POKEYSND_Update(POKEY_OFFSET_AUDCTL, byte, 0, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDF1:
		POKEY_AUDF[POKEY_CHAN1] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDF1, byte, 0, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDF2:
		POKEY_AUDF[POKEY_CHAN2] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDF2, byte, 0, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDF3:
		POKEY_AUDF[POKEY_CHAN3] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDF3, byte, 0, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDF4:
		POKEY_AUDF[POKEY_CHAN4] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDF4, byte, 0, SOUND_GAIN);
		break;
	case POKEY_OFFSET_STIMER:
		POKEYSND_Update(POKEY_OFFSET_STIMER, byte, 0, SOUND_GAIN);
		break;
#ifdef STEREO_SOUND
	case POKEY_OFFSET_AUDC1 + POKEY_OFFSET_POKEY2:
		POKEY_AUDC[POKEY_CHAN1 + POKEY_CHIP2] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDC1, byte, 1, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDC2 + POKEY_OFFSET_POKEY2:
		POKEY_AUDC[POKEY_CHAN2 + POKEY_CHIP2] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDC2, byte, 1, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDC3 + POKEY_OFFSET_POKEY2:
		POKEY_AUDC[POKEY_CHAN3 + POKEY_CHIP2] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDC3, byte, 1, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDC4 + POKEY_OFFSET_POKEY2:
		POKEY_AUDC[POKEY_CHAN4 + POKEY_CHIP2] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDC4, byte, 1, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDCTL + POKEY_OFFSET_POKEY2:
		POKEY_AUDCTL[1] = byte;
		/* determine the base multiplier for the 'div by n' calculations */
		if (byte & POKEY_CLOCK_15)
			POKEY_Base_mult[1] = POKEY_DIV_15;
		else
			POKEY_Base_mult[1] = POKEY_DIV_64;

		POKEYSND_Update(POKEY_OFFSET_AUDCTL, byte, 1, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDF1 + POKEY_OFFSET_POKEY2:
		POKEY_AUDF[POKEY_CHAN1 + POKEY_CHIP2] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDF1, byte, 1, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDF2 + POKEY_OFFSET_POKEY2:
		POKEY_AUDF[POKEY_CHAN2 + POKEY_CHIP2] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDF2, byte, 1, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDF3 + POKEY_OFFSET_POKEY2:
		POKEY_AUDF[POKEY_CHAN3 + POKEY_CHIP2] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDF3, byte, 1, SOUND_GAIN);
		break;
	case POKEY_OFFSET_AUDF4 + POKEY_OFFSET_POKEY2:
		POKEY_AUDF[POKEY_CHAN4 + POKEY_CHIP2] = byte;
		POKEYSND_Update(POKEY_OFFSET_AUDF4, byte, 1, SOUND_GAIN);
		break;
	case POKEY_OFFSET_STIMER + POKEY_OFFSET_POKEY2:
		POKEYSND_Update(POKEY_OFFSET_STIMER, byte, 1, SOUND_GAIN);
		break;
#endif
	default:
		break;
	}
}

static void POKEY_Initialise(void)
{
	int i;
	ULONG reg;

	for (i = 0; i < (POKEY_MAXPOKEYS * 4); i++) {
		POKEY_AUDC[i] = 0;
		POKEY_AUDF[i] = 0;
	}

	for (i = 0; i < POKEY_MAXPOKEYS; i++) {
		POKEY_AUDCTL[i] = 0;
		POKEY_Base_mult[i] = POKEY_DIV_64;
	}

	/* initialise poly9_lookup */
	reg = 0x1ff;
	for (i = 0; i < 511; i++) {
		reg = ((((reg >> 5) ^ reg) & 1) << 8) + (reg >> 1);
		POKEY_poly9_lookup[i] = (UBYTE) reg;
	}
	/* initialise poly17_lookup */
	reg = 0x1ffff;
	for (i = 0; i < 16385; i++) {
		reg = ((((reg >> 5) ^ reg) & 0xff) << 9) + (reg >> 8);
		POKEY_poly17_lookup[i] = (UBYTE) (reg >> 1);
	}

	random_scanline_counter = 0;
}

UBYTE ASAP_GetByte(UWORD addr)
{
	unsigned int i;
	switch (addr & 0xff0f) {
	case 0xd20a:
		i = random_scanline_counter + (unsigned int) ANTIC_xpos + (unsigned int) ANTIC_xpos / LINE_C * DMAR;
		if (POKEY_AUDCTL[0] & POKEY_POLY9)
			return POKEY_poly9_lookup[i % POKEY_POLY9_SIZE];
		else {
			const UBYTE *ptr;
			i %= POKEY_POLY17_SIZE;
			ptr = POKEY_poly17_lookup + (i >> 3);
			i &= 7;
			return (UBYTE) ((ptr[0] >> i) + (ptr[1] << (8 - i)));
		}
	case 0xd40b:
		return (UBYTE) ((unsigned int) ANTIC_xpos / (unsigned int) (2 * (LINE_C - DMAR)) % 156U);
	default:
		return MEMORY_dGetByte(addr);
	}
}

void ASAP_PutByte(UWORD addr, UBYTE byte)
{
	/* TODO: implement WSYNC */
#if 0
	if ((addr >> 8) == 0xd2)
		POKEY_PutByte(addr, byte);
	else if ((addr & 0xff0f) == 0xd40a) {
		if (ANTIC_xpos <= WSYNC_C && ANTIC_xpos_limit >= WSYNC_C)
			ANTIC_xpos = WSYNC_C;
		else {
			ANTIC_wsync_halt = TRUE;
			ANTIC_xpos = ANTIC_xpos_limit;
		}
	}
	else
		MEMORY_dPutByte(addr, byte);
#else
	POKEY_PutByte(addr, byte);
#endif
}

/* We use CIM opcode to return from a subroutine to ASAP */
void ASAP_CIM(void)
{
	ANTIC_xpos = ANTIC_xpos_limit;
}

static unsigned int block_rate;
static unsigned int sample_format;
static unsigned int sample_16bit;

void ASAP_Initialize(unsigned int frequency, unsigned int audio_format, unsigned int quality)
{
	block_rate = frequency;
	sample_format = audio_format;
	sample_16bit = audio_format == AUDIO_FORMAT_U8 ? 0 : 1;
	enable_stereo = 5; /* force Pokey_sound_init() in ASAP_PlaySong() */
	POKEY_Initialise();
	if (quality == 0)
		POKEYSND_enable_new_pokey = FALSE;
	else {
		POKEYSND_SetMzQuality(quality - 1);
		POKEYSND_enable_new_pokey = TRUE;
	}
}

static char sap_type;
static UWORD sap_player = 0;
static UWORD sap_music = 0;
static UWORD sap_music_offset = 0;
static UWORD sap_music_length = 0;
static UWORD sap_init = 0;
static unsigned int sap_stereo;
static unsigned int sap_songs;
static unsigned int sap_defsong;
static unsigned int sap_fastplay = 312;
static unsigned int sap_reg_output = 0;

/* This array maps subsong numbers to track positions for MPT and RMT formats. */
static UBYTE song_pos[128];
static int sap_song_duration[128] = { 0 };

static unsigned int tmc_per_frame;
static unsigned int tmc_per_frame_counter;

static const unsigned int perframe2fastplay[] = { 312U, 312U / 2U, 312U / 3U, 312U / 4U };

int ASAP_get_fastplay() { return sap_fastplay; }
int ASAP_get_stereo() { return sap_stereo; }
int ASAP_get_type() { return sap_type; }
int ASAP_get_duration(int song) { return sap_song_duration[song]; }
void ASAP_set_reg_output() { sap_reg_output = 1; }

static int load_native(const unsigned char *module, unsigned int module_len,
                       const unsigned char *player, char type)
{
	UWORD player_last_byte;
	int block_len;
	if (module[0] != 0xff || module[1] != 0xff)
		return FALSE;
	sap_player = player[2] + (player[3] << 8);
	player_last_byte = player[4] + (player[5] << 8);
	sap_music = module[2] + (module[3] << 8);
	if (sap_music <= player_last_byte)
		return FALSE;
	block_len = module[4] + (module[5] << 8) + 1 - sap_music;
	if ((unsigned int) (6 + block_len) != module_len) {
		UWORD info_addr;
		int info_len;
		if (type != 'r' || (unsigned int) (11 + block_len) > module_len)
			return FALSE;
		/* allow optional info for Raster Music Tracker */
		info_addr = module[6 + block_len] + (module[7 + block_len] << 8);
		if (info_addr != sap_music + block_len)
			return FALSE;
		info_len = module[8 + block_len] + (module[9 + block_len] << 8) + 1 - info_addr;
		if ((unsigned int) (10 + block_len + info_len) != module_len)
			return FALSE;
	}
	memcpy(memory + sap_music, module + 6, block_len);
	memcpy(memory + sap_player, player + 6, player_last_byte + 1 - sap_player);
	sap_type = type;
	return TRUE;
}

static int load_cmc(const unsigned char *module, unsigned int module_len, int cmr)
{
	int pos;
	if (module_len < 0x300)
		return FALSE;
	if (!load_native(module, module_len, cmc_obx, 'C'))
		return FALSE;
	if (cmr)
		memcpy(memory + 0x500 + CMR_BASS_TABLE_OFFSET, cmr_bass_table, sizeof(cmr_bass_table));
	/* auto-detect number of subsongs */
	pos = 0x54;
	while (--pos >= 0) {
		if (module[0x206 + pos] < 0xfe
		 || module[0x25b + pos] < 0xfe
		 || module[0x2b0 + pos] < 0xfe)
			break;
	}
	while (--pos >= 0) {
		if (module[0x206 + pos] == 0x8f || module[0x206 + pos] == 0xef)
			sap_songs++;
	}
	return TRUE;
}

static int load_mpt(const unsigned char *module, unsigned int module_len)
{
	unsigned int i;
	unsigned int song_len;
	/* seen[i] == TRUE if the track position i is already processed */
	UBYTE seen[256];
	if (module_len < 0x1d0)
		return FALSE;
	if (!load_native(module, module_len, mpt_obx, 'm'))
		return FALSE;
	/* do not auto-detect number of subsongs if the address
	   of the first track is non-standard */
	if (module[0x1c6] + (module[0x1ca] << 8) != sap_music + 0x1ca) {
		song_pos[0] = 0;
		return TRUE;
	}
	/* Calculate the length of the first track. Address of the second track minus
	   address of the first track equals the length of the first track in bytes.
	   Divide by two to get number of track positions. */
	song_len = (module[0x1c7] + (module[0x1cb] << 8) - sap_music - 0x1ca) >> 1;
	if (song_len > 0xfe)
		return FALSE;
	memset(seen, FALSE, sizeof(seen));
	sap_songs = 0;
	for (i = 0; i < song_len; i++) {
		unsigned int j;
		UBYTE c;
		if (seen[i])
			continue;
		j = i;
		/* follow jump commands until a pattern or a stop command is found */
		do {
			seen[j] = TRUE;
			c = module[0x1d0 + j * 2];
			if (c != 0xff)
				break;
			j = module[0x1d1 + j * 2];
		} while (j < song_len && !seen[j]);
		/* if no pattern found then this is not a subsong */
		if (c >= 64)
			continue;
		/* found subsong */
		song_pos[sap_songs++] = (UBYTE) j;
		j++;
		/* follow this subsong */
		while (j < song_len && !seen[j]) {
			seen[j] = TRUE;
			c = module[0x1d0 + j * 2];
			if (c < 64)
				j++;
			else if (c == 0xff)
				j = module[0x1d1 + j * 2];
			else
				break;
		}
	}
	return sap_songs != 0;
}

static int load_rmt(const unsigned char *module, unsigned int module_len)
{
	unsigned int i;
	UWORD song_start;
	UWORD song_last_byte;
	const unsigned char *song;
	unsigned int song_len;
	UBYTE seen[256];
	if (module_len < 0x30 || module[6] != 'R' || module[7] != 'M'
	 || module[8] != 'T' || module[13] != 1)
		return FALSE;
	switch (module[9]) {
	case '4':
		break;
	case '8':
		sap_stereo = 1;
		break;
	default:
		return FALSE;
	}
	i = module[12];
	if (i < 1 || i > 4)
		return FALSE;
	sap_fastplay = perframe2fastplay[i - 1];
	if (!load_native(module, module_len, sap_stereo ? rmt8_obx : rmt4_obx, 'r'))
		return FALSE;
	sap_player = 0x600;
	/* auto-detect number of subsongs */
	song_start = module[20] + (module[21] << 8);
	song_last_byte = module[4] + (module[5] << 8);
	if (song_start <= sap_music || song_start >= song_last_byte)
		return FALSE;
	song = module + 6 + song_start - sap_music;
	song_len = (song_last_byte + 1 - song_start) >> (2 + sap_stereo);
	if (song_len > 0xfe)
		song_len = 0xfe;
	memset(seen, FALSE, sizeof(seen));
	sap_songs = 0;
	for (i = 0; i < song_len; i++) {
		unsigned int j;
		if (seen[i])
			continue;
		j = i;
		do {
			seen[j] = TRUE;
			if (song[j << (2 + sap_stereo)] != 0xfe)
				break;
			j = song[(j << (2 + sap_stereo)) + 1];
		} while (j < song_len && !seen[j]);
		song_pos[sap_songs++] = (UBYTE) j;
		j++;
		while (j < song_len && !seen[j]) {
			seen[j] = TRUE;
			if (song[j << (2 + sap_stereo)] != 0xfe)
				j++;
			else
				j = song[(j << (2 + sap_stereo)) + 1];
		}
	}
	return sap_songs != 0;
}

static int load_tmc(const unsigned char *module, unsigned int module_len)
{
	unsigned int i;
	if (module_len < 0x1d0)
		return FALSE;
	if (!load_native(module, module_len, tmc_obx, 't'))
		return FALSE;
	tmc_per_frame = module[37];
	if (tmc_per_frame < 1 || tmc_per_frame > 4)
		return FALSE;
	sap_fastplay = perframe2fastplay[tmc_per_frame - 1];
	i = 0;
	/* find first instrument */
	while (module[0x66 + i] == 0) {
		if (++i >= 64)
			return FALSE; /* no instrument */
	}
	i = (module[0x66 + i] << 8) + module[0x26 + i] - sap_music - 1 + 6;
	if (i >= module_len)
		return FALSE;
	/* skip trailing jumps */
	do {
		if (i <= 0x1b5)
			return FALSE; /* no pattern to play */
		i -= 16;
	} while (module[i] >= 0x80);
	while (i >= 0x1b5) {
		if (module[i] >= 0x80)
			sap_songs++;
		i -= 16;
	}
	return TRUE;
}

static int load_tm2(const unsigned char *module, unsigned int module_len)
{
	unsigned int i;
	unsigned int song_end;
	unsigned char c;
	if (module_len < 0x3a4)
		return FALSE;
	i = module[0x25];
	if (i < 1 || i > 4)
		return FALSE;
	sap_fastplay = perframe2fastplay[i - 1];
	if (!load_native(module, module_len, tm2_obx, 'T'))
		return FALSE;
	/* TODO: quadrophonic */
	if (module[0x1f] != 0)
#ifdef STEREO_SOUND
		sap_stereo = 1;
#else
		return FALSE;
#endif
	sap_player = 0x500;
	song_end = 0xffff;
	for (i = 0; i < 0x80; i++) {
		unsigned int instr_addr = module[0x86 + i] + (module[0x306 + i] << 8);
		if (instr_addr != 0 && instr_addr < song_end)
			song_end = instr_addr;
	}
	for (i = 0; i < 0x100; i++) {
		unsigned int pattern_addr = module[0x106 + i] + (module[0x206 + i] << 8);
		if (pattern_addr != 0 && pattern_addr < song_end)
			song_end = pattern_addr;
	}
	if (song_end < sap_music + 0x380U + 2U * 17U)
		return FALSE;
	i = song_end - sap_music - 0x380;
	if (0x386 + i >= module_len)
		return FALSE;
	i -= i % 17;
	/* skip trailing stop/jump commands */
	do {
		if (i == 0)
			return FALSE;
		i -= 17;
		c = module[0x386 + 16 + i];
	} while (c == 0 || c >= 0x80);
	/* count stop/jump commands */
	while (i > 0) {
		i -= 17;
		c = module[0x386 + 16 + i];
		if (c == 0 || c >= 0x80)
			sap_songs++;
	}
	return TRUE;
}

static int tag_matches(const char *tag, const UBYTE *sap_ptr, const UBYTE *sap_end)
{
	size_t len = strlen(tag);
	return (sap_ptr + len + 8 < sap_end) && memcmp(tag, sap_ptr, len) == 0;
}

static int parse_hex(const UBYTE **ps, UWORD *retval)
{
	int chars = 0;
	*retval = 0;
	while (**ps != 0x0d) {
		char c;
		if (++chars > 4)
			return FALSE;
		c = (char) *(*ps)++;
		*retval <<= 4;
		if (c >= '0' && c <= '9')
			*retval += c - '0';
		else if (c >= 'A' && c <= 'F')
			*retval += c - 'A' + 10;
		else if (c >= 'a' && c <= 'f')
			*retval += c - 'a' + 10;
		else
			return FALSE;
	}
	return chars != 0;
}

static int parse_dec(const UBYTE **ps, unsigned int *retval)
{
	int chars = 0;
	*retval = 0;
	while (**ps != 0x0d) {
		char c;
		if (++chars > 4)
			return FALSE;
		c = (char) *(*ps)++;
		*retval *= 10;
		if (c >= '0' && c <= '9')
			*retval += c - '0';
		else
			return FALSE;
	}
	return chars != 0;
}

// parses strings as milliseconds from mm:ss.dddd
static int ASAP_ParseDurationN(const char *s, int len)
{
	int factor = 1;
	int n = 0;
	int t = 0;

	while ( len > 0)
	{
		len--;
		if(isdigit(s[len]))
		{
			n += factor * (s[len]-'0');
			factor *= 10;
		}
		else if(s[len] == '.')
		{
			t += n*1000/factor;
			factor = 1;
			n = 0;
		}
		else if(s[len] == ':')
		{
			t += n*1000;
			n = 0;
			factor = 60;
		}
		else if(len >= 4 && !strncmp(s + len - 4, " LOOP", 5))
		{
			len-=4;
		}
		else
			return -1;
	}
	t += n*1000;

	return t;
}

int ASAP_ParseDuration(const char *s)
{
	return ASAP_ParseDurationN(s, strlen(s));
}

static int load_sap(const UBYTE *sap_ptr, const UBYTE * const sap_end)
{
	int song = 0;
	int abin = 0;
	if (!tag_matches("SAP", sap_ptr, sap_end))
		return FALSE;
	sap_type = '?';
	sap_player = 0xffff;
	sap_music = 0xffff;
	sap_init = 0xffff;
	sap_ptr += 3;
	for (;;) {
		if (sap_ptr + 8 >= sap_end || sap_ptr[0] != 0x0d || sap_ptr[1] != 0x0a)
			return FALSE;
		sap_ptr += 2;
		if (sap_ptr[0] == 0xff)
		{
			abin = 1;
			break;
		}
		if (sap_ptr[0] == '\n')
		{
			sap_ptr ++;
			break;
		}
		if (sap_ptr[0] == '\r' && sap_ptr[1] == '\n' )
		{
			sap_ptr += 2;
			break;
		}
		if (tag_matches("TYPE ", sap_ptr, sap_end)) {
			sap_ptr += 5;
			sap_type = *sap_ptr++;
		}
		else if (tag_matches("PLAYER ", sap_ptr, sap_end)) {
			sap_ptr += 7;
			if (!parse_hex(&sap_ptr, &sap_player))
				return FALSE;
		}
		else if (tag_matches("MUSIC ", sap_ptr, sap_end)) {
			sap_ptr += 6;
			if (!parse_hex(&sap_ptr, &sap_music))
				return FALSE;
		}
		else if (tag_matches("INIT ", sap_ptr, sap_end)) {
			sap_ptr += 5;
			if (!parse_hex(&sap_ptr, &sap_init))
				return FALSE;
		}
		else if (tag_matches("SONGS ", sap_ptr, sap_end)) {
			sap_ptr += 6;
			if (!parse_dec(&sap_ptr, &sap_songs) || sap_songs < 1 || sap_songs > 255)
				return FALSE;
		}
		else if (tag_matches("DEFSONG ", sap_ptr, sap_end)) {
			sap_ptr += 8;
			if (!parse_dec(&sap_ptr, &sap_defsong))
				return FALSE;
		}
		else if (tag_matches("FASTPLAY ", sap_ptr, sap_end)) {
			sap_ptr += 9;
			if (!parse_dec(&sap_ptr, &sap_fastplay) || sap_fastplay < 1 )
				return FALSE;
		}
		else if (tag_matches("STEREO", sap_ptr, sap_end))
#ifdef STEREO_SOUND
			sap_stereo = 1;
#else
			return FALSE;
#endif
		else if (tag_matches("TIME ", sap_ptr, sap_end)) {
			sap_ptr += 5;
			int n = 0;
			const char *s = (const char *)sap_ptr;
			for(;*sap_ptr != 0x0d;sap_ptr++, n++)
				if(sap_ptr > sap_end) return FALSE;

			if(n == 0) return FALSE;
			sap_song_duration[song] = ASAP_ParseDurationN(s, n);
			if(sap_song_duration[song] < 0) return FALSE;
			song++;
		}
		/* ignore unknown tags */
		while (sap_ptr[0] != 0x0d) {
			sap_ptr++;
			if (sap_ptr >= sap_end)
				return FALSE;
		}
	}
	if (sap_defsong >= sap_songs)
		return FALSE;
	switch (sap_type) {
	case 'B':
		if (sap_player == 0xffff || sap_init == 0xffff)
			return FALSE;
		break;
	case 'C':
		if (sap_player == 0xffff || sap_music == 0xffff)
			return FALSE;
	case 'R':
		{
			if(abin)
				sap_ptr += 6;
			sap_music_length = (int)(sap_end - sap_ptr);
			memset(memory, 0, sizeof(memory));
			memcpy(memory, sap_ptr, sap_music_length);
		}
			return TRUE;
		break;
	default:
		return FALSE;
	}
	if (sap_ptr[1] != 0xff)
		return FALSE;
	memset(memory, 0, sizeof(memory));
	sap_ptr += 2;
	while (sap_ptr + 5 <= sap_end) {
		int start_addr = sap_ptr[0] + (sap_ptr[1] << 8);
		int block_len = sap_ptr[2] + (sap_ptr[3] << 8) + 1 - start_addr;
		if (block_len <= 0 || sap_ptr + block_len > sap_end)
			return FALSE;
		sap_ptr += 4;
		memcpy(memory + start_addr, sap_ptr, block_len);
		sap_ptr += block_len;
		if (sap_ptr == sap_end)
			return TRUE;
		if (sap_ptr + 7 <= sap_end && sap_ptr[0] == 0xff && sap_ptr[1] == 0xff)
			sap_ptr += 2;
	}
	return FALSE;
}

#define EXT(c1, c2, c3) ((c1 + (c2 << 8) + (c3 << 16)) | 0x202020)

int ASAP_Load(const char *filename, const unsigned char *module, unsigned int module_len)
{
	const char *p;
	int ext;
	for (p = filename; *p != '\0'; p++);
	ext = 0;
	for (;;) {
		if (--p <= filename || *p < ' ')
			return FALSE; /* no filename extension or invalid character */
		if (*p == '.')
			break;
		ext = (ext << 8) + (*p & 0xff);
	}
	sap_stereo = 0;
	sap_songs = 1;
	sap_defsong = 0;
	sap_fastplay = 312;
	switch (ext | 0x202020) {
	case EXT('C', 'M', 'C'):
		return load_cmc(module, module_len, FALSE);
	case EXT('C', 'M', 'R'):
		return load_cmc(module, module_len, TRUE);
	case EXT('D', 'M', 'C'):
		sap_fastplay = 156;
		return load_cmc(module, module_len, FALSE);
	case EXT('M', 'P', 'D'):
		sap_fastplay = 156;
		return load_mpt(module, module_len);
	case EXT('M', 'P', 'T'):
		return load_mpt(module, module_len);
	case EXT('R', 'M', 'T'):
		return load_rmt(module, module_len);
	case EXT('S', 'A', 'P'):
		return load_sap(module, module + module_len);
	case EXT('T', 'M', '2'):
		return load_tm2(module, module_len);
#ifdef STEREO_SOUND
	case EXT('T', 'M', '8'):
		sap_stereo = 1;
		return load_tmc(module, module_len);
#endif
	case EXT('T', 'M', 'C'):
		return load_tmc(module, module_len);
	default:
		return FALSE;
	}
}

unsigned int ASAP_GetChannels(void)
{
	return 1 << sap_stereo;
}

unsigned int ASAP_GetSongs(void)
{
	return sap_songs;
}

unsigned int ASAP_GetDefSong(void)
{
	return sap_defsong;
}

unsigned int ASAP_GetDuration(int song)
{
	return sap_song_duration[song];
}

static void call_6502(UWORD addr, int max_scanlines)
{
	CPU_regPC = addr;
	/* put a CIM at 0xd20a and a return address on stack */
	MEMORY_dPutByte(0xd20a, 0xd2);
	MEMORY_dPutByte(0x01fe, 0x09);
	MEMORY_dPutByte(0x01ff, 0xd2);
	CPU_regS = 0xfd;
	ANTIC_xpos = 0;
	CPU_GO(max_scanlines * (LINE_C - DMAR));
}

/* 50 Atari frames for the initialization routine - some SAPs are self-extracting. */
#define SCANLINES_FOR_INIT  (50 * 312)

void ASAP_PlaySong(unsigned int song)
{
	UWORD addr;
	if (enable_stereo != sap_stereo) {
		POKEYSND_Init(ASAP_MAIN_CLOCK, block_rate,
			1 << sap_stereo, sample_16bit ? POKEYSND_BIT16 : 0);
		enable_stereo = sap_stereo;
	}
	for (addr = POKEY_OFFSET_AUDF1; addr <= POKEY_OFFSET_STIMER; addr++)
		POKEY_PutByte(addr, 0);
	if (sap_stereo)
		for (addr = POKEY_OFFSET_AUDF1 + POKEY_OFFSET_POKEY2; addr <= POKEY_OFFSET_STIMER + POKEY_OFFSET_POKEY2; addr++)
			POKEY_PutByte(addr, 0);
	CPU_regP = 0x30;
	switch (sap_type) {
	case 'B':
		CPU_regA = (UBYTE) song;
		CPU_regX = 0x00;
		CPU_regY = 0x00;
		/* 5 frames should be enough */
		call_6502(sap_init, SCANLINES_FOR_INIT);
		break;
	case 'C':
		CPU_regA = 0x70;
		CPU_regX = (UBYTE) sap_music;
		CPU_regY = (UBYTE) (sap_music >> 8);
		call_6502((UWORD) (sap_player + 3), SCANLINES_FOR_INIT);
		CPU_regA = 0x00;
		CPU_regX = (UBYTE) song;
		call_6502((UWORD) (sap_player + 3), SCANLINES_FOR_INIT);
		break;
	case 'm':
		CPU_regA = 0x00;
		CPU_regX = (UBYTE) (sap_music >> 8);
		CPU_regY = (UBYTE) sap_music;
		call_6502(sap_player, SCANLINES_FOR_INIT);
		CPU_regA = 0x02;
		CPU_regX = song_pos[song];
		call_6502(sap_player, SCANLINES_FOR_INIT);
		break;
	case 'r':
		CPU_regA = song_pos[song];
		CPU_regX = (UBYTE) sap_music;
		CPU_regY = (UBYTE) (sap_music >> 8);
		call_6502(sap_player, SCANLINES_FOR_INIT);
		break;
	case 't':
	case 'T':
		CPU_regA = 0x70;
		CPU_regX = (UBYTE) (sap_music >> 8);
		CPU_regY = (UBYTE) sap_music;
		call_6502(sap_player, SCANLINES_FOR_INIT);
		CPU_regA = 0x00;
		CPU_regX = (UBYTE) song;
		call_6502(sap_player, SCANLINES_FOR_INIT);
		tmc_per_frame_counter = 1;
		break;
	}
}

static int ASAP_Cycle()
{
	int i, p;

	switch (sap_type) {
	case 'B':
		call_6502(sap_player, sap_fastplay);
		break;
	case 'C':
		call_6502((UWORD) (sap_player + 6), sap_fastplay);
	case 'R':
		if(sap_music_offset >= sap_music_length) return 0;

		int s = 9; /* AUDF/AUDCTL */
		for(p = 0; s > 0 && p < POKEY_MAXPOKEYS; p++)
		{
			for(i = 0; s > 0 && i < 4; i++)
			{
				POKEY_PutByte(p*POKEY_OFFSET_POKEY2+i*2, memory[sap_music_offset++]);
				s--;
				if(!s) break;
				POKEY_PutByte(p*POKEY_OFFSET_POKEY2+i*2+1, memory[sap_music_offset++]);
				s--;
			}
			if(s) { POKEY_PutByte(p*POKEY_OFFSET_POKEY2+8, memory[sap_music_offset++]); s--; }
		}
		break;
	case 'm':
	case 'r':
	case 'T':
		call_6502((UWORD) (sap_player + 3), sap_fastplay);
		break;
	case 't':
		if (--tmc_per_frame_counter <= 0) {
			tmc_per_frame_counter = tmc_per_frame;
			call_6502((UWORD) (sap_player + 3), sap_fastplay);
		}
		else
			call_6502((UWORD) (sap_player + 6), sap_fastplay);
		break;
	}
	random_scanline_counter = (random_scanline_counter + LINE_C * sap_fastplay)
		                          % ((POKEY_AUDCTL[0] & POKEY_POLY9) ? POKEY_POLY9_SIZE : POKEY_POLY17_SIZE);
	return 1;
}

int ASAP_GenerateR(void *buffer, unsigned int buffer_len)
{
	int ret = 0;
	int i, p;

	while(buffer_len >= (9 << sap_stereo))
	{
		if(!ASAP_Cycle()) break;

		for(p = 0; p < 1 + sap_stereo; p++)
		{
			for(i = 0; i < 4; i++)
			{
				((unsigned char *)buffer)[ret++] = POKEY_AUDF[p*4+i];
				((unsigned char *)buffer)[ret++] = POKEY_AUDC[p*4+i];
			}
			((unsigned char *)buffer)[ret++] = POKEY_AUDCTL[p];
		}
		buffer_len -= (9 << sap_stereo);
	}
	return ret;
}

int ASAP_Generate(void *buffer, unsigned int buffer_len)
{
	int ret = 0;
	int samples;

	if(sap_reg_output)
		return ASAP_GenerateR(buffer, buffer_len);

	/* convert number of bytes to number of blocks */
	buffer_len >>= sample_16bit + enable_stereo;

	samples = block_rate / 50 * (1.0*sap_fastplay/312);
	samples <<= enable_stereo;

	while(buffer_len >= samples)
	{
		if(!ASAP_Cycle()) break;
		
		POKEYSND_Process(buffer, samples);

		ret += samples << sample_16bit;
			/* swap bytes in non-native words if necessary */
			if (sample_format ==
#ifdef WORDS_BIGENDIAN
				AUDIO_FORMAT_S16_LE
#else
				AUDIO_FORMAT_S16_BE
#endif
				) {
				unsigned char *p = (unsigned char *) buffer;
				unsigned int n = samples;
				do {
					unsigned char t = p[0];
					p[0] = p[1];
					p[1] = t;
					p += 2;
				} while (--n != 0U);
			}
break;
		buffer_len -= samples;
		buffer += (samples << sample_16bit);
	}
	return ret;
}
