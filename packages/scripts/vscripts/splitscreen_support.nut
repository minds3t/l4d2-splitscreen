//=============================================================================
// Splitscreen Support, native package (included by director_base_addon.nut)
//
// The splitscreen_support plugin does the join, the controllers and the keyboard. This
// script covers what only the game's script API can do for player two:
//
// * Leaving and coming back during a game (pause menu, engine commands
//   ss_disconnect / ss_connect). A returning player connects as a spectator;
//   put them back on their survivor, or on a free one after a map change.
// * Local team modes (Versus, Scavenge, Realism Versus, Versus Survival): keep
//   the server from shutting down a game whose humans are all on one team.
//
// It acts only while a local split-screen player exists, so it is inert in
// ordinary games and without the plugin.
//=============================================================================

if ( !( "SplitscreenSupport" in getroottable() ) )
{
	::SplitscreenSupport <-
	{
		thinker = null
		guestUserId = null

		// Player two's last survivor, and the state of a leave / rejoin.
		lastCharacter = null
		leaving = false
		leftUserId = null
		leftTeam = null
		leftCharacter = null
		rejoinTries = 0
		strandedTicks = 0

		// Local competitive rule (see ConfigureLocalCompetitive).
		previousBotGame = null
		previousHibernate = null
		competitiveConfigured = false

		// On PC the split-screen guest is a human with network ID BOT. Survivor
		// bots have that ID too, but IsPlayerABot identifies them. Requiring
		// the listen host keeps this off dedicated and remote servers.
		function GetLocalGuest()
		{
			local host = GetListenServerHost();
			if ( host == null )
				return null;
			local player = null;
			while ( player = Entities.FindByClassname( player, "player" ) )
			{
				if ( player != host && !IsPlayerABot( player ) && player.GetNetworkIDString() == "BOT" )
					return player;
			}
			return null;
		}

		// The survivor bot an idle ("take a break") player owns.
		function IdleSurvivorOf( userid )
		{
			local bot = null;
			while ( bot = Entities.FindByClassname( bot, "player" ) )
			{
				if ( IsPlayerABot( bot ) && bot.IsSurvivor() &&
					NetProps.GetPropInt( bot, "m_humanSpectatorUserID" ) == userid )
					return bot;
			}
			return null;
		}

		// A survivor bot no idle human owns: the wanted character if it is
		// free, otherwise any. "jointeam 2" without a name does not seat a
		// player who joined after a map change, so a name is always sent.
		function FreeSurvivor( wanted )
		{
			local bot = null, other = null;
			while ( bot = Entities.FindByClassname( bot, "player" ) )
			{
				if ( !IsPlayerABot( bot ) || !bot.IsSurvivor() ||
					NetProps.GetPropInt( bot, "m_humanSpectatorUserID" ) != 0 )
					continue;
				local name = GetCharacterDisplayName( bot );
				if ( name == wanted )
					return name;
				if ( other == null )
					other = name;
			}
			return other;
		}

		// Native competitive games require humans on both teams and shut an
		// "empty" team's game down. Two local players may share a team. Permit
		// that through Valve's bot-game rule, without sv_cheats.
		function ConfigureLocalCompetitive()
		{
			// Every mode with a human infected team, whatever its base mode
			// (Versus Survival is based on Survival).
			if ( !HasPlayerControlledZombies() )
				return;
			if ( !competitiveConfigured )
			{
				previousBotGame = Convars.GetFloat( "sb_all_bot_game" );
				if ( previousBotGame == null )
					return;
				// sb_all_bot_game's callback also disables hibernation; save
				// that value before the callback changes it.
				previousHibernate = Convars.GetFloat( "sv_hibernate_when_empty" );
				if ( previousHibernate != null )
					Convars.SetValue( "sv_hibernate_when_empty", 0 );
				competitiveConfigured = true;
				printl( "[Splitscreen] Local competitive game: empty-team shutdown disabled (sb_all_bot_game 1)" );
			}
			if ( Convars.GetFloat( "sb_all_bot_game" ) != 1 )
				Convars.SetValue( "sb_all_bot_game", 1 );
		}

		function RestoreLocalCompetitive()
		{
			if ( !competitiveConfigured )
				return;
			if ( Convars.GetFloat( "sb_all_bot_game" ) != previousBotGame )
				Convars.SetValue( "sb_all_bot_game", previousBotGame );
			if ( previousHibernate != null && Convars.GetFloat( "sv_hibernate_when_empty" ) != previousHibernate )
				Convars.SetValue( "sv_hibernate_when_empty", previousHibernate );
			previousBotGame = null;
			previousHibernate = null;
			competitiveConfigured = false;
			printl( "[Splitscreen] Restored the competitive session rule after player two left" );
		}

		// Remembers player two's survivor while they play, and notices a player
		// two who joined a running game with no record of an earlier survivor
		// (the record does not survive a map change): they would stay a
		// spectator, so give them a free survivor.
		function TrackGuest( guest )
		{
			local team = NetProps.GetPropInt( guest, "m_iTeamNum" );
			local survivor = team == 2 ? guest : IdleSurvivorOf( guest.GetPlayerUserId() );
			if ( survivor != null )
			{
				lastCharacter = GetCharacterDisplayName( survivor );
				strandedTicks = 0;
				return;
			}
			if ( team == 3 || HasPlayerControlledZombies() || ++strandedTicks < 4 )
				return;
			strandedTicks = 0;
			leftTeam = 2;
			leftCharacter = null;
			rejoinTries = 8;
			printl( "[Splitscreen] Player 2 joined a running game: taking a free survivor" );
		}

		function RestoreGuest( guest )
		{
			if ( NetProps.GetPropInt( guest, "m_iTeamNum" ) == leftTeam )
			{
				rejoinTries = 0;
				// The HUD of a player who was not there when the map loaded was
				// never set up for this map (no health panel, cross or crosshair).
				SendToServerConsole( "hud_reloadscheme" );
				printl( "[Splitscreen] Player 2 is back in the game" );
				return;
			}
			rejoinTries--;
			// jointeam 2 <name> makes the player the idle owner of that survivor
			// bot; spec_next then takes control, as for any returning player.
			local command = "cmd2 jointeam " + leftTeam;
			if ( leftTeam == 2 )
			{
				local character = FreeSurvivor( leftCharacter );
				if ( character == null )
					return;
				command += " " + character;
			}
			SendToServerConsole( command );
			if ( leftTeam == 2 )
				SendToServerConsole( "cmd2 spec_next" );
		}

		function Think()
		{
			local guest = GetLocalGuest();
			if ( guest == null )
				return;
			local userid = guest.GetPlayerUserId();
			// Still the departing player until a new connection replaces it.
			if ( leaving && userid == leftUserId )
				return;
			if ( userid != guestUserId )
			{
				guestUserId = userid;
				strandedTicks = 0;
				if ( leaving )
				{
					leaving = false;
					if ( leftTeam == 2 || leftTeam == 3 )
						rejoinTries = 8;
					printl( "[Splitscreen] Player 2 rejoined" );
				}
				ConfigureLocalCompetitive();
			}
			if ( rejoinTries > 0 )
				RestoreGuest( guest );
			else
				TrackGuest( guest );
		}

		// Entities spawned by scripts do not survive a round restart, so every
		// lifecycle event makes sure the thinker exists.
		function Watch()
		{
			if ( IsDedicatedServer() || ( thinker != null && thinker.IsValid() ) )
				return;
			thinker = SpawnEntityFromTable( "logic_script", { targetname = UniqueString( "ss_player_two" ) } );
			if ( thinker == null )
				return;
			thinker.ValidateScriptScope();
			thinker.GetScriptScope()["SSPlayerTwo"] <- function()
			{
				::SplitscreenSupport.Think();
				return 1.0;
			};
			AddThinkToEnt( thinker, "SSPlayerTwo" );
		}

		// Registered from director_base_addon.nut so the listeners survive map start.
		function OnGameEvent_player_activate( params ) { Watch(); }
		function OnGameEvent_player_connect_full( params ) { Watch(); }
		function OnGameEvent_player_spawn( params ) { Watch(); }
		function OnGameEvent_round_start( params ) { Watch(); }

		function OnGameEvent_player_disconnect( params )
		{
			if ( guestUserId == null || !( "userid" in params ) || params.userid != guestUserId )
				return;
			local player = GetPlayerFromUserID( params.userid );
			leftUserId = params.userid;
			leftTeam = null;
			leftCharacter = null;
			if ( player != null && player.IsValid() )
			{
				leftTeam = NetProps.GetPropInt( player, "m_iTeamNum" );
				if ( leftTeam == 2 )
					leftCharacter = GetCharacterDisplayName( player );
				else if ( leftTeam != 3 )
				{
					// An idle ("take a break") player counts as a spectator; the
					// pause menu's leave row hands the survivor to a bot this way.
					leftCharacter = lastCharacter;
					leftTeam = 2;
				}
			}
			rejoinTries = 0;
			strandedTicks = 0;
			leaving = true;
			guestUserId = null;
			RestoreLocalCompetitive();
		}
	}
}
