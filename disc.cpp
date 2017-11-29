
#include "libretro.h"
#include <string/stdstring.h>

#include "mednafen/mednafen-types.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#include "mednafen/cdrom/cdromif.h"
#include "mednafen/hash/md5.h"
#include "mednafen/hash/sha256.h"
#include "mednafen/ss/ss.h"
#include "mednafen/ss/cdb.h"
#include "mednafen/ss/smpc.h"

//------------------------------------------------------------------------------
// Locals
//------------------------------------------------------------------------------

static bool g_eject_state;

static int g_current_disc;

static std::vector<CDIF *> CDInterfaces;

//
// Remember to rebuild region database in db.cpp if changing the order of
// entries in this table(and be careful about game id collisions,
// e.g. with some Korean games).
//
static const struct
{
	const char c;
	const char* str;	// Community-defined region string that may appear in filename.
	unsigned region;
}
region_strings[] =
{
	// Listed in order of preference for multi-region games.
	{ 'U', "USA", SMPC_AREA_NA },
	{ 'J', "Japan", SMPC_AREA_JP },
	{ 'K', "Korea", SMPC_AREA_KR },

	{ 'E', "Europe", SMPC_AREA_EU_PAL },
	{ 'E', "Germany", SMPC_AREA_EU_PAL },
	{ 'E', "France", SMPC_AREA_EU_PAL },
	{ 'E', "Spain", SMPC_AREA_EU_PAL },

	{ 'B', "Brazil", SMPC_AREA_CSA_NTSC },

	{ 'T', "Asia_NTSC", SMPC_AREA_ASIA_NTSC },
	{ 'A', "Asia_PAL", SMPC_AREA_ASIA_PAL },
	{ 'L', "CSA_PAL", SMPC_AREA_CSA_PAL },
};


//------------------------------------------------------------------------------
// Local Functions
//------------------------------------------------------------------------------

static void ReadM3U( std::vector<std::string> &file_list, std::string path, unsigned depth = 0 )
{
	std::string dir_path;
	char linebuf[ 2048 ];
	FILE *fp = fopen(path.c_str(), "rb");
	if (fp == NULL)
		return;

	MDFN_GetFilePathComponents(path, &dir_path);

	while(fgets(linebuf, sizeof(linebuf), fp) != NULL)
	{
		std::string efp;

		if(linebuf[0] == '#')
			continue;
		string_trim_whitespace_right(linebuf);
		if(linebuf[0] == 0)
			continue;

		efp = MDFN_EvalFIP(dir_path, std::string(linebuf));
		if(efp.size() >= 4 && efp.substr(efp.size() - 4) == ".m3u")
		{
			if(efp == path)
			{
				log_cb(RETRO_LOG_ERROR, "M3U at \"%s\" references self.\n", efp.c_str());
				goto end;
			}

			if(depth == 99)
			{
				log_cb(RETRO_LOG_ERROR, "M3U load recursion too deep!\n");
				goto end;
			}

			ReadM3U(file_list, efp, depth++);
		}
		else
		{
			file_list.push_back(efp);
		}
	}

end:
	fclose(fp);
}

static bool IsSaturnDisc( const uint8* sa32k )
{
	if(sha256(&sa32k[0x100], 0xD00) != "96b8ea48819cfa589f24c40aa149c224c420dccf38b730f00156efe25c9bbc8f"_sha256)
		return false;

	if(memcmp(&sa32k[0], "SEGA SEGASATURN ", 16))
		return false;

	log_cb(RETRO_LOG_INFO, "This is a Saturn disc.\n");
	return true;
}

static bool disk_set_eject_state( bool ejected )
{
	if ( ejected == g_eject_state )
	{
		// no change
		return false;
	}
	else
	{
		// store
		g_eject_state = ejected;

		if ( ejected )
		{
			// open disc tray
			CDB_SetDisc( true, NULL );
		}
		else
		{
			// close the tray - with a disc inside
			if ( g_current_disc < CDInterfaces.size() ) {
				CDB_SetDisc( false, CDInterfaces[g_current_disc] );
			} else {
				CDB_SetDisc( false, NULL );
			}
		}

		return true;
	}
}

static bool disk_get_eject_state(void)
{
	return g_eject_state;
}

static unsigned disk_get_image_index(void)
{
	return g_current_disc;
}

static bool disk_set_image_index(unsigned index)
{
	// only listen if the tray is open
	if ( g_eject_state == true )
	{
		if ( index < CDInterfaces.size() ) {
			// log_cb(RETRO_LOG_INFO, "Selected disc %d of %d.\n", index+1, CDInterfaces.size() );
			g_current_disc = index;
			return true;
		}
	}

	return false;
}

static unsigned disk_get_num_images(void)
{
	return CDInterfaces.size();
}

/*
#if 0
// Mednafen really doesn't support adding disk images on the fly ...
// Hack around this.
static void update_md5_checksum(CDIF *iface)
{
   uint8 LayoutMD5[16];
   md5_context layout_md5;
   CD_TOC toc;

   md5_starts(&layout_md5);

   TOC_Clear(&toc);

   iface->ReadTOC(&toc);

   md5_update_u32_as_lsb(&layout_md5, toc.first_track);
   md5_update_u32_as_lsb(&layout_md5, toc.last_track);
   md5_update_u32_as_lsb(&layout_md5, toc.tracks[100].lba);

   for (uint32 track = toc.first_track; track <= toc.last_track; track++)
   {
      md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].lba);
      md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].control & 0x4);
   }

   md5_finish(&layout_md5, LayoutMD5);
   memcpy(MDFNGameInfo->MD5, LayoutMD5, 16);

   char *md5 = md5_asciistr(MDFNGameInfo->MD5);
   log_cb(RETRO_LOG_INFO, "[Mednafen]: Updated md5 checksum: %s.\n", md5);
}
#endif
*/
static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
	log_cb(RETRO_LOG_INFO, "disk_replace_image_index(%d,*info) called.\n", index);
	return false;

	// todo - untested

#if 0
   if (index >= disk_get_num_images() || !g_eject_state)
      return false;

   if (!info)
   {
      delete CDInterfaces.at(index);
      CDInterfaces.erase(CDInterfaces.begin() + index);
      if (index < CD_SelectedDisc)
         CD_SelectedDisc--;

      // Poke into psx.cpp
      CalcDiscSCEx();
      return true;
   }

   bool success = true;
   CDIF *iface = CDIF_Open(&success, info->path, false, false);

   if (!success)
      return false;

   delete CDInterfaces.at(index);
   CDInterfaces.at(index) = iface;
   CalcDiscSCEx();

   /* If we replace, we want the "swap disk manually effect". */
   extract_basename(retro_cd_base_name, info->path, sizeof(retro_cd_base_name));
   /* Ugly, but needed to get proper disk swapping effect. */
   update_md5_checksum(iface);
   return true;
#endif
}

static bool disk_add_image_index(void)
{
	log_cb(RETRO_LOG_INFO, "disk_add_image_index called.\n");

	CDInterfaces.push_back(NULL);
	return true;
}


//------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------

static struct retro_disk_control_callback disk_interface =
{
	disk_set_eject_state,
	disk_get_eject_state,
	disk_get_image_index,
	disk_set_image_index,
	disk_get_num_images,
	disk_replace_image_index,
	disk_add_image_index,
};

void disc_init( retro_environment_t environ_cb )
{
	// start closed
	g_eject_state = false;

	// register vtable with environment
	environ_cb( RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_interface );
}

void disc_calcgameid( uint8* id_out16, uint8* fd_id_out16, char* sgid )
{
	md5_context mctx;
	uint8_t buf[2048];

	log_cb(RETRO_LOG_INFO, "Calculating game ID (%d discs)\n", CDInterfaces.size() );

	mctx.starts();

	for(size_t x = 0; x < CDInterfaces.size(); x++)
	{
		CDIF *c = CDInterfaces[x];
		TOC toc;

		c->ReadTOC(&toc);

		mctx.update_u32_as_lsb(toc.first_track);
		mctx.update_u32_as_lsb(toc.last_track);
		mctx.update_u32_as_lsb(toc.disc_type);

		for(unsigned i = 1; i <= 100; i++)
		{
			const auto& t = toc.tracks[i];

			mctx.update_u32_as_lsb(t.adr);
			mctx.update_u32_as_lsb(t.control);
			mctx.update_u32_as_lsb(t.lba);
			mctx.update_u32_as_lsb(t.valid);
		}

		for(unsigned i = 0; i < 512; i++)
		{
			if(c->ReadSector((uint8_t*)&buf[0], i, 1) >= 0x1)
			{
				if(i == 0)
				{
					char* tmp;
					memcpy(sgid, (void*)(&buf[0x20]), 16);
					sgid[16] = 0;
					if((tmp = strrchr(sgid, 'V')))
					{
						do
						{
						*tmp = 0;
						} while(tmp-- != sgid && (signed char)*tmp <= 0x20);
					}
				}

				mctx.update(&buf[0], 2048);
			}
		}

		if(x == 0)
		{
			md5_context fd_mctx = mctx;
			fd_mctx.finish(fd_id_out16);
		}
	}

	mctx.finish(id_out16);
}

void disc_cleanup()
{
	for(unsigned i = 0; i < CDInterfaces.size(); i++) {
		delete CDInterfaces[i];
	}
	CDInterfaces.clear();

	g_current_disc = 0;
}

bool disc_detect_region( unsigned* region )
{
	uint8_t *buf = new uint8[2048 * 16];
	uint64 possible_regions = 0;

	for(auto& c : CDInterfaces)
	{
		if(c->ReadSector(&buf[0], 0, 16) != 0x1)
			continue;

		if(!IsSaturnDisc(&buf[0]))
			continue;

		for(unsigned i = 0; i < 16; i++)
		{
			for(auto const& rs : region_strings)
			{
				if(rs.c == buf[0x40 + i])
				{
					possible_regions |= (uint64)1 << rs.region;
					break;
				}
			}
		}

		break;
	}

	delete[] buf;

	for(auto const& rs : region_strings)
	{
		if(possible_regions & ((uint64)1 << rs.region))
		{
			log_cb(RETRO_LOG_INFO, "Disc Region: \"%s\"\n", rs.str );
			*region = rs.region;
			return true;
		}
	}

	return false;
}

bool disc_test()
{
	size_t i;

	// For each disc
	for( i = 0; i < CDInterfaces.size(); i++ )
	{
		TOC toc;

		CDInterfaces[i]->ReadTOC(&toc);

		// For each track
		for( int32 track = 1; track <= 99; track++)
		{
			if(!toc.tracks[track].valid)
				continue;

			if(toc.tracks[track].control & SUBQ_CTRLF_DATA)
				continue;

			//
			//
			//

			const int32 start_lba = toc.tracks[track].lba;
			const int32 end_lba = start_lba + 32 - 1;
			bool any_subq_curpos = false;

			for(int32 lba = start_lba; lba <= end_lba; lba++)
			{
				uint8 pwbuf[96];
				uint8 qbuf[12];

				if(!CDInterfaces[i]->ReadRawSectorPWOnly(pwbuf, lba, false))
				{
					log_cb(RETRO_LOG_ERROR,
						"Testing Disc %zu of %zu: Error reading sector at LBA %d.\n",
							i + 1, CDInterfaces.size(), lba );
					return false;
				}

				subq_deinterleave(pwbuf, qbuf);
				if(subq_check_checksum(qbuf) && (qbuf[0] & 0xF) == ADR_CURPOS)
				{
					const uint8 qm = qbuf[7];
					const uint8 qs = qbuf[8];
					const uint8 qf = qbuf[9];
					uint8 lm, ls, lf;

					any_subq_curpos = true;

					LBA_to_AMSF(lba, &lm, &ls, &lf);
					lm = U8_to_BCD(lm);
					ls = U8_to_BCD(ls);
					lf = U8_to_BCD(lf);

					if(lm != qm || ls != qs || lf != qf)
					{
						log_cb(RETRO_LOG_ERROR,
							"Testing Disc %zu of %zu: Time mismatch at LBA=%d(%02x:%02x:%02x); Q subchannel: %02x:%02x:%02x\n",
								i + 1, CDInterfaces.size(),
								lba,
								lm, ls, lf,
								qm, qs, qf);

						return false;
					}
				}
			}

			if(!any_subq_curpos)
			{
				log_cb(RETRO_LOG_ERROR,
					  "Testing Disc %zu of %zu: No valid Q subchannel ADR_CURPOS data present at LBA %d-%d?!\n",
					  	i + 1, CDInterfaces.size(),
					  	start_lba, end_lba );
				return false;
			}

			break;

		}; // for each track

	}; // for each disc

	return true;
}

void disc_select( unsigned disc_num )
{
	if ( disc_num < CDInterfaces.size() ) {
		g_current_disc = disc_num;
		CDB_SetDisc( false, CDInterfaces[ g_current_disc ] );
	}
}

bool disc_load_content( MDFNGI* game_interface, const char* content_name, uint8* fd_id, char* sgid )
{
	disc_cleanup();

	if ( !content_name ) {
		return false;
	}

	uint8 LayoutMD5[ 16 ];

	log_cb( RETRO_LOG_INFO, "Loading \"%s\"\n", content_name );

	try
	{
		size_t content_name_len;
		content_name_len = strlen( content_name );
		if ( content_name_len > 4 )
		{
			const char* content_ext = content_name + content_name_len - 4;
			if ( !strcasecmp( content_ext, ".m3u" ) )
			{
				// multiple discs
				std::vector<std::string> file_list;
				ReadM3U(file_list, content_name);
				for(unsigned i = 0; i < file_list.size(); i++)
				{
					bool success = true;
					log_cb(RETRO_LOG_INFO, "Adding CD: \"%s\".\n", file_list[i].c_str());
					CDIF *image  = CDIF_Open(file_list[i].c_str(), false);
					CDInterfaces.push_back(image);
				}
			}
			else
			{
				// single disc
				bool success = true;
				CDIF *image  = CDIF_Open(content_name, false);
				CDInterfaces.push_back(image);
			}
		}
	}
	catch( std::exception &e )
	{
		log_cb(RETRO_LOG_ERROR, "Loading Failed.\n");
		return false;
	}

	// Print out a track list for all discs.
	for(unsigned i = 0; i < CDInterfaces.size(); i++)
	{
		TOC toc;
		CDInterfaces[i]->ReadTOC(&toc);
		log_cb(RETRO_LOG_DEBUG, "Disc %d\n", i + 1);
		for(int32 track = toc.first_track; track <= toc.last_track; track++) {
			log_cb(RETRO_LOG_DEBUG, "- Track %2d, LBA: %6d  %s\n", track, toc.tracks[track].lba, (toc.tracks[track].control & 0x4) ? "DATA" : "AUDIO");
		}
		log_cb(RETRO_LOG_DEBUG, "Leadout: %6d\n", toc.tracks[100].lba);
	}

	log_cb(RETRO_LOG_DEBUG, "Calculating layout MD5.\n");
	// Calculate layout MD5.  The system emulation LoadCD() code is free to ignore this value and calculate
	// its own, or to use it to look up a game in its database.
	{
		md5_context layout_md5;
		layout_md5.starts();

		for( unsigned i = 0; i < CDInterfaces.size(); i++ )
		{
			TOC toc;

			CDInterfaces[i]->ReadTOC(&toc);

			layout_md5.update_u32_as_lsb(toc.first_track);
			layout_md5.update_u32_as_lsb(toc.last_track);
			layout_md5.update_u32_as_lsb(toc.tracks[100].lba);

			for(uint32 track = toc.first_track; track <= toc.last_track; track++)
			{
				layout_md5.update_u32_as_lsb(toc.tracks[track].lba);
				layout_md5.update_u32_as_lsb(toc.tracks[track].control & 0x4);
			}
		}

		layout_md5.finish(LayoutMD5);
	}
	log_cb(RETRO_LOG_DEBUG, "Done calculating layout MD5.\n");
	// TODO: include module name in hash

	memcpy( game_interface->MD5, LayoutMD5, 16 );

	disc_calcgameid( game_interface->MD5, fd_id, sgid );

	return true;
}

//==============================================================================
