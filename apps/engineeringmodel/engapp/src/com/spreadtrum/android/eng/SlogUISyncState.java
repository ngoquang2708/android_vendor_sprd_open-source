package com.spreadtrum.android.eng;

/**
 * The configures in SlogUIActivities are all depence on
 * slog.conf file, and different activities all control
 * one slog.conf file, they *must* be syncable. And the
 * path of slog.conf is given in SlogAction. See @SlogAction
 */
public interface SlogUISyncState {

    /**
     * Using syncState to make sure you have newest controls
     * showing on screen.
     */
     void syncState();
     
     /** TODO: at last, we'll add a Listener here to watch
      *       the changing of slog.conf. Underconstruction.
      */
      void onSlogConfigChanged();
}
