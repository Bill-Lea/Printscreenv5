Scriptname Printscreen_PlayerRef_Script extends ReferenceAlias
{Attached to a ReferenceAlias that MUST be filled with the Player reference.
 OnPlayerLoadGame only fires on a player-filled ReferenceAlias.}

Printscreen_MainQuest_script Property MainQuest Auto

; BUG FIX: this event was previously named OnGameLoad(). No such event exists
; on ReferenceAlias — the Papyrus compiler accepts unknown event definitions
; without complaint, but the engine never invokes them. The correct event is
; OnPlayerLoadGame(), which fires on the player-filled alias every time a
; save is loaded. Because OnGameLoad never fired, MainQuest's
; InitializePrintscreen() (hotkey + mod-event re-registration) never ran on
; any game load, so a mid-playthrough install could end up with a dead or
; unrecoverable TakePhoto key.
Event OnPlayerLoadGame()
    if (MainQuest)
        MainQuest.OnPlayerLoadGame()
    endif
EndEvent
