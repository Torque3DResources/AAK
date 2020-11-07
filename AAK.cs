function AAK::onCreate(%this)
{
}

function AAK::onDestroy(%this)
{
}

//This is called when the server is initially set up by the game application
function AAK::initServer(%this)
{
   %this.queueExec("./scripts/server/shapeBase.cs");
	%this.queueExec("./scripts/server/aiPlayer.cs");
	%this.queueExec("./scripts/server/camera.cs");
	%this.queueExec("./scripts/server/commands.cs");
	%this.queueExec("./scripts/server/player.cs");
	%this.queueExec("./scripts/server/ActionAdventureGame.cs");

	%this.queueExec("./scripts/server/spawn.cs");
}

//This is called when the server is created for an actual game/map to be played
function AAK::onCreateGameServer(%this)
{
   %this.registerDatablock("./scripts/datablocks/aiPlayer.cs");
	%this.registerDatablock("./scripts/datablocks/camera.cs");
	%this.registerDatablock("./scripts/datablocks/dust.cs");
	%this.registerDatablock("./scripts/datablocks/markers.cs");
	%this.registerDatablock("./scripts/datablocks/player.cs");
	%this.registerDatablock("./scripts/datablocks/triggers.cs");
}

//This is called when the server is shut down due to the game/map being exited
function AAK::onDestroyGameServer(%this)
{
}

//This is called when the client is initially set up by the game application
function AAK::initClient(%this)
{
   if (!$Server::Dedicated)
   {
      //client scripts
      $KeybindPath = "data/AAK/scripts/client/default.keybinds.cs";
      exec($KeybindPath);
      
      %prefPath = getPrefpath();
      if(isFile(%prefPath @ "/keybinds.cs"))
         exec(%prefPath @ "/keybinds.cs");
      
      //gui
      //

      exec("data/AAK/scripts/client/inputCommands.cs");
   }
}

//This is called when a client connects to a server
function AAK::onCreateClientConnection(%this)
{
}

//This is called when a client disconnects from a server
function AAK::onDestroyClientConnection(%this)
{
}
