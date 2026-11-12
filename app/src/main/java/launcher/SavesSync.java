package launcher;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * Copia de los saves en la carpeta Castlevania del almacenamiento compartido, SIN "acceso a todos los archivos":
 * el usuario elige esa carpeta una vez con el selector de Android (ACTION_OPEN_DOCUMENT_TREE) y el permiso queda
 * guardado. Todo se lee y escribe por DocumentsContract; no hay rutas /storage/... de por medio.
 *
 * Los saves del juego viven en getExternalFilesDir()/hdd (la ExeDir del codigo nativo). La copia queda en
 * <carpeta elegida>/saves_backup. Android borra el permiso al desinstalar, asi que tras reinstalar hay que elegir la
 * carpeta otra vez (un toque); la copia en si no se pierde.
 */
public final class SavesSync {
    private static final String TAG = "SavesSync";
    private static final String PREFS = "saves_sync", KEY_TREE = "tree";
    private static final String BACKUP_DIR = "saves_backup";
    private static final int READ_WRITE = Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION;

    private static final Object LOCK = new Object();
    // despues de un restore a mano la copia se deja como esta hasta que la app arranque otra vez: el juego que sigue
    // abierto tiene su propia idea de los saves y no debe escribir encima
    private static volatile boolean held;
    private static Thread thread;

    private SavesSync() {}

    // ---- la carpeta elegida ----

    /** La carpeta elegida, o null si no hay o Android ya quito el permiso. */
    public static Uri tree(Context c) {
        String s = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_TREE, null);
        if (s == null) return null;
        Uri u = Uri.parse(s);
        for (UriPermission p : c.getContentResolver().getPersistedUriPermissions())
            if (p.getUri().equals(u) && p.isReadPermission() && p.isWritePermission()) return u;
        return null;
    }

    public static boolean isSavesTree(Context c, Uri u) {
        String s = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_TREE, null);
        return s != null && s.equals(u.toString());
    }

    /** Guarda la carpeta que dio el selector; lanza si Android no deja conservar el permiso. */
    public static void setTree(Context c, Uri uri) {
        c.getContentResolver().takePersistableUriPermission(uri, READ_WRITE);
        String old = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_TREE, null);
        if (old != null && !old.equals(uri.toString())) {
            try {
                c.getContentResolver().releasePersistableUriPermission(Uri.parse(old), READ_WRITE);
            } catch (Exception ignored) {}
        }
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString(KEY_TREE, uri.toString()).apply();
    }

    /** Nombre corto de la carpeta para mostrarlo (por ejemplo "Castlevania"). */
    public static String treeName(Uri tree) {
        String id = DocumentsContract.getTreeDocumentId(tree);
        int i = id.lastIndexOf(':');
        return i >= 0 ? id.substring(i + 1) : id;
    }

    // ---- API publica ----

    /** Trae de la copia los archivos que el juego aun no tiene; nunca pisa uno que ya existe. -1 sin carpeta o sin copia. */
    public static int restoreMissing(Context c) {
        Uri tree = tree(c);
        if (tree == null) return -1;
        synchronized (LOCK) {
            ContentResolver r = c.getContentResolver();
            String root = backupRoot(r, tree, false);
            if (root == null) return -1;
            File hdd = hdd(c);
            hdd.mkdirs();
            int n = mirrorDown(r, tree, root, hdd, hdd.getAbsolutePath(), false);
            Log.i(TAG, "restoreMissing: " + n + " archivos");
            return n;
        }
    }

    /**
     * El boton RESTAURAR: la copia pasa a ser el save del juego. Lo que habia se guarda antes en
     * getExternalFilesDir()/hdd_before_restore. -1 sin carpeta o sin copia, -2 no se pudo preparar el destino.
     */
    public static int restoreNow(Context c) {
        Uri tree = tree(c);
        if (tree == null) return -1;
        synchronized (LOCK) {
            ContentResolver r = c.getContentResolver();
            String root = backupRoot(r, tree, false);
            if (root == null || countFiles(r, tree, root) == 0) return -1;
            held = true;
            File hdd = hdd(c);
            File before = new File(c.getExternalFilesDir(null), "hdd_before_restore");
            deleteAll(before);
            if (hdd.exists() && !hdd.renameTo(before)) return -2;
            hdd.mkdirs();
            int n = mirrorDown(r, tree, root, hdd, hdd.getAbsolutePath(), true);
            Log.i(TAG, "restoreNow: " + n + " archivos");
            return n;
        }
    }

    /** Hay una copia que restaurar en la carpeta elegida. */
    public static boolean hasBackup(Context c) {
        Uri tree = tree(c);
        if (tree == null) return false;
        ContentResolver r = c.getContentResolver();
        String root = backupRoot(r, tree, false);
        return root != null && countFiles(r, tree, root) > 0;
    }

    /** Hilo que mantiene la copia al dia; se llama al arrancar el proceso, una sola vez. */
    public static synchronized void startBackup(Context context) {
        if (thread != null) return;
        final Context app = context.getApplicationContext();
        thread = new Thread(() -> backupLoop(app), "saves-backup");
        thread.setDaemon(true);
        thread.start();
    }

    // ---- el hilo de la copia ----

    private static void backupLoop(Context c) {
        long[] last = {0, 0};
        boolean first = true, warned = false;
        for (;;) {
            try {
                Thread.sleep(5000);
                if (held) continue;
                Uri tree = tree(c);
                if (tree == null) continue;
                File hdd = hdd(c);
                long[] now = signature(hdd);
                if (now[0] == 0 || (now[0] == last[0] && now[1] == last[1])) continue;
                // que haya cambiado y siga igual un momento despues: un archivo a medio escribir no se copia
                Thread.sleep(2000);
                long[] settled = signature(hdd);
                if (settled[0] != now[0] || settled[1] != now[1]) continue;
                synchronized (LOCK) {
                    if (held) continue;
                    ContentResolver r = c.getContentResolver();
                    String root = backupRoot(r, tree, true);
                    if (root == null) {
                        if (!warned) Log.w(TAG, "no se pudo crear " + BACKUP_DIR + " en la carpeta elegida");
                        warned = true;
                        continue;
                    }
                    if (first) {
                        // la copia tenia mas archivos que el juego: se guarda una copia aparte antes de escribir
                        File prev = new File(c.getExternalFilesDir(null), "saves_backup_prev");
                        if (!prev.exists() && countFiles(r, tree, root) > settled[0]) {
                            prev.mkdirs();
                            mirrorDown(r, tree, root, prev, prev.getAbsolutePath(), false);
                        }
                    }
                    int[] fail = {0};
                    int n = mirrorUp(r, tree, root, hdd, fail);
                    if (n > 0 || first) Log.i(TAG, n + " archivos copiados");
                    first = false;
                    if (fail[0] == 0) last = settled;
                }
            } catch (InterruptedException e) {
                return;
            } catch (Throwable t) {
                Log.w(TAG, "backup: " + t);
            }
        }
    }

    // ---- DocumentsContract ----

    private static final class Doc {
        String id, name;
        boolean dir;
        long size, time;
    }

    private static File hdd(Context c) { return new File(c.getExternalFilesDir(null), "hdd"); }

    private static List<Doc> children(ContentResolver r, Uri tree, String parentId) {
        List<Doc> out = new ArrayList<>();
        Uri uri = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        try (Cursor c = r.query(uri, new String[]{Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
                Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED}, null, null, null)) {
            while (c != null && c.moveToNext()) {
                Doc d = new Doc();
                d.id = c.getString(0);
                d.name = c.getString(1);
                d.dir = Document.MIME_TYPE_DIR.equals(c.getString(2));
                d.size = c.getLong(3);
                d.time = c.getLong(4);
                if (d.id != null && d.name != null) out.add(d);
            }
        } catch (Exception e) {
            Log.w(TAG, "no pude listar " + parentId + ": " + e);
        }
        return out;
    }

    private static Doc find(List<Doc> list, String name) {
        for (Doc d : list) if (d.name.equals(name)) return d;
        return null;
    }

    private static String createDir(ContentResolver r, Uri tree, String parentId, String name) {
        try {
            Uri made = DocumentsContract.createDocument(r, DocumentsContract.buildDocumentUriUsingTree(tree, parentId),
                    Document.MIME_TYPE_DIR, name);
            return made == null ? null : DocumentsContract.getDocumentId(made);
        } catch (Exception e) {
            Log.w(TAG, "no pude crear " + name + ": " + e);
            return null;
        }
    }

    // el id de saves_backup dentro de la carpeta elegida; null si no esta (y no se pidio crearla)
    private static String backupRoot(ContentResolver r, Uri tree, boolean create) {
        String rootId;
        try {
            rootId = DocumentsContract.getTreeDocumentId(tree);
        } catch (Exception e) {
            return null;
        }
        Doc d = find(children(r, tree, rootId), BACKUP_DIR);
        if (d != null) return d.dir ? d.id : null;
        return create ? createDir(r, tree, rootId, BACKUP_DIR) : null;
    }

    private static int countFiles(ContentResolver r, Uri tree, String parentId) {
        int n = 0;
        for (Doc d : children(r, tree, parentId)) n += d.dir ? countFiles(r, tree, d.id) : 1;
        return n;
    }

    // los archivos del juego hacia la copia: los que faltan o han cambiado. Devuelve los copiados; fail[0] cuenta fallos
    private static int mirrorUp(ContentResolver r, Uri tree, String parentId, File dir, int[] fail) {
        File[] files = dir.listFiles();
        if (files == null) return 0;
        List<Doc> remote = children(r, tree, parentId);
        int copied = 0;
        for (File f : files) {
            Doc d = find(remote, f.getName());
            if (f.isDirectory()) {
                String id = d != null ? (d.dir ? d.id : null) : createDir(r, tree, parentId, f.getName());
                if (id == null) fail[0]++;
                else copied += mirrorUp(r, tree, id, f, fail);
            } else if (f.isFile()) {
                if (d != null && !d.dir && d.size == f.length() && d.time >= f.lastModified()) continue;
                if (d != null && d.dir) {
                    fail[0]++;
                    continue;
                }
                try {
                    Uri target = d != null ? DocumentsContract.buildDocumentUriUsingTree(tree, d.id)
                            : DocumentsContract.createDocument(r, DocumentsContract.buildDocumentUriUsingTree(tree, parentId),
                                    "application/octet-stream", f.getName());
                    if (target == null) throw new IOException("sin destino");
                    try (InputStream in = new FileInputStream(f); OutputStream out = r.openOutputStream(target, "wt")) {
                        if (out == null) throw new IOException("no se puede escribir");
                        pipe(in, out);
                    }
                    copied++;
                } catch (Exception e) {
                    Log.w(TAG, f.getName() + ": " + e);
                    fail[0]++;
                }
            }
        }
        return copied;
    }

    // la copia hacia una carpeta del telefono; sin overwrite solo trae lo que falta. Devuelve los archivos copiados
    private static int mirrorDown(ContentResolver r, Uri tree, String parentId, File dir, String rootPath,
                                  boolean overwrite) {
        dir.mkdirs();
        int copied = 0;
        for (Doc d : children(r, tree, parentId)) {
            // el nombre lo da el proveedor de archivos: no se confia en que se quede dentro de la carpeta
            if (d.name.contains("/") || d.name.equals(".") || d.name.equals("..")) continue;
            File out = new File(dir, d.name);
            try {
                if (!out.getCanonicalPath().startsWith(rootPath + File.separator)) continue;
            } catch (IOException e) {
                continue;
            }
            if (d.dir) {
                copied += mirrorDown(r, tree, d.id, out, rootPath, overwrite);
                continue;
            }
            if (!overwrite && out.exists()) continue;
            File tmp = new File(out.getPath() + ".tmp");
            try (InputStream in = r.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, d.id));
                 OutputStream os = new FileOutputStream(tmp)) {
                if (in == null) throw new IOException("no se puede leer");
                pipe(in, os);
            } catch (Exception e) {
                Log.w(TAG, d.name + ": " + e);
                tmp.delete();
                continue;
            }
            if (out.exists()) out.delete();
            if (!tmp.renameTo(out)) {
                tmp.delete();
                continue;
            }
            if (d.time > 0) out.setLastModified(d.time);
            copied++;
        }
        return copied;
    }

    private static void pipe(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[256 * 1024];
        int n;
        while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
    }

    // cuantos archivos hay y un numero que cambia si se anade, cambia de tamano o se reescribe alguno
    private static long[] signature(File dir) {
        long[] s = {0, 0};
        sign(dir, s);
        return s;
    }

    private static void sign(File dir, long[] s) {
        File[] list = dir.listFiles();
        if (list == null) return;
        for (File f : list) {
            if (f.isDirectory()) {
                sign(f, s);
            } else if (f.isFile()) {
                s[0]++;
                s[1] = s[1] * 1099511628211L ^ (f.length() + f.lastModified() * 31 + f.getPath().hashCode());
            }
        }
    }

    private static void deleteAll(File f) {
        File[] list = f.listFiles();
        if (list != null) for (File x : list) deleteAll(x);
        f.delete();
    }
}
