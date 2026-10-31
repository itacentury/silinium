// Replaces the stock maps/mp/gametypes/_clientids.gsc, the gametype scripts call init() on every map.
#include maps\mp\_utility;
#include common_scripts\utility;

init()
{
    level.clientid = 0;

    setMemory("0x10042000", "12345678");
    setMemory("0x10042010", "4578656D706C65206F6620737472696E67");

    level thread onPlayerConnect();
}

onPlayerConnect()
{
    for(;;)
    {
        level waittill("connecting", player);

        // Stock behavior
        player.clientId = level.clientid;
        level.clientid++;

        player thread onPlayerSpawned();
    }
}

onPlayerSpawned()
{
    self endon("disconnect");
    for(;;)
    {
        self waittill("spawned_player");
        self iprintln("Hello from ^1replaced ^7GSC");
    }
}
