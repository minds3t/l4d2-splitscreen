// Splitscreen Support, native package: player two's leave / rejoin handling.
// Game event listeners only work when registered from this script.
IncludeScript( "splitscreen_support" );
__CollectEventCallbacks( ::SplitscreenSupport, "OnGameEvent_", "GameEventCallbacks", RegisterScriptGameEventListener );
