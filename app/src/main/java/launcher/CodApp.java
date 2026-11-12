package launcher;

import android.app.Application;

/** Arranca el hilo de la copia de saves con el proceso, lo abra quien lo abra (launcher o juego restaurado por Android). */
public class CodApp extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        SavesSync.startBackup(this);
    }
}
