package launcher;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.text.InputType;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Pantalla previa al juego: elegir el .iso (o la carpeta) del juego y activar el contador de FPS y la resolucion real.
 * Escribe launcher.txt en getExternalFilesDir() (la ExeDir del codigo nativo) y abre SDLActivity. El codigo nativo
 * borra launcher.txt al leerlo y deja una copia en launcher_last.txt: de ahi se rellena la ruta la proxima vez.
 *
 * No pide "acceso a todos los archivos": el .iso se queda donde esta y se guarda su URI (content://) con permiso
 * persistente; el codigo nativo lo abre por su descriptor. Una carpeta con default.xbe se copia al almacenamiento
 * propio de la app (getExternalFilesDir()/assets), que no necesita permisos.
 */
public class LauncherActivity extends Activity {
    // la actividad del juego; cambiala por CodActivity si tu proyecto la usa
    private static final String GAME_ACTIVITY = "org.libsdl.app.SDLActivity";
    private static final int PICK_FOLDER = 1, PICK_ISO = 2, PICK_SAVES = 3;

    // paleta
    private static final int BG = Color.parseColor("#0D0A12");
    private static final int PANEL = Color.parseColor("#1A141F");
    private static final int PANEL_LINE = Color.parseColor("#3A2A3F");
    private static final int GOLD = Color.parseColor("#C9A24B");
    private static final int CRIMSON = Color.parseColor("#8E1B2D");
    private static final int TEXT = Color.parseColor("#EDE6D8");
    private static final int MUTED = Color.parseColor("#9A8F9E");

    private EditText path;
    private CheckBox fps, resolution;
    private TextView status;
    private Button languageButton, playButton, savesFolderButton, restoreButton;
    private TextView savesInfo;
    // idiomas de la consola Xbox (XC_LANGUAGE): codigo y nombre; el juego europeo trae estos cinco
    private static final int[] LANG_CODES = {1, 3, 4, 5, 6};
    private static final String[] LANG_NAMES = {"English", "Deutsch", "Fran\u00e7ais", "Espa\u00f1ol", "Italiano"};
    private int language = 1;

    // mapeo del mando: el boton i de la Xbox lee el boton fisico padMap[i]. Los 16 botones se numeran como los
    // cuenta el codigo nativo: 0-7 A B X Y Black White LT RT, 8-15 cruceta arriba/abajo/izq/der, Start, Select, L3, R3
    private static final String[] OUT_NAMES = {"A", "B", "X", "Y", "Black", "White", "LT", "RT",
            "Cruceta arriba", "Cruceta abajo", "Cruceta izquierda", "Cruceta derecha",
            "Start", "Select", "Clic stick izquierdo", "Clic stick derecho"};
    private static final String[] SRC_NAMES = {"A", "B", "X", "Y", "R1", "L1", "L2", "R2",
            "Cruceta arriba", "Cruceta abajo", "Cruceta izquierda", "Cruceta derecha",
            "Start", "Select", "L3", "R3"};
    private final int[] padMap = new int[16];
    private final Button[] mapButtons = new Button[16];

    private void showLanguage() {
        String name = LANG_NAMES[0];
        for (int i = 0; i < LANG_CODES.length; i++) if (LANG_CODES[i] == language) name = LANG_NAMES[i];
        languageButton.setText(name);
    }

    // el boton fisico (codigo de tecla de Android) como numero de boton, -1 si no es de los 16
    private static int slotOfKey(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A: return 0;
            case KeyEvent.KEYCODE_BUTTON_B: return 1;
            case KeyEvent.KEYCODE_BUTTON_X: return 2;
            case KeyEvent.KEYCODE_BUTTON_Y: return 3;
            case KeyEvent.KEYCODE_BUTTON_R1: return 4;
            case KeyEvent.KEYCODE_BUTTON_L1: return 5;
            case KeyEvent.KEYCODE_BUTTON_L2: return 6;
            case KeyEvent.KEYCODE_BUTTON_R2: return 7;
            case KeyEvent.KEYCODE_DPAD_UP: return 8;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 9;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 10;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 11;
            case KeyEvent.KEYCODE_BUTTON_START: return 12;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return 13;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return 14;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return 15;
            default: return -1;
        }
    }

    private void loadMap() {
        for (int i = 0; i < 16; i++) padMap[i] = i;
        String[] parts = readSaved("pad_map").split(",");
        if (parts.length != 16) return;
        int[] parsed = new int[16];
        try {
            for (int i = 0; i < 16; i++) {
                parsed[i] = Integer.parseInt(parts[i].trim());
                if (parsed[i] < 0 || parsed[i] > 15) return;
            }
        } catch (Exception e) {
            return;
        }
        System.arraycopy(parsed, 0, padMap, 0, 16);
    }

    private String mapText() {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 16; i++) sb.append(i == 0 ? "" : ",").append(padMap[i]);
        return sb.toString();
    }

    private void showMap() {
        for (int i = 0; i < 16; i++) {
            mapButtons[i].setText(SRC_NAMES[padMap[i]]);
            mapButtons[i].setTextColor(padMap[i] == i ? TEXT : GOLD);
        }
    }

    // se elige de la lista, o se pulsa el boton en el mando (los gatillos tambien); la cruceta del mando solo mueve
    // la lista, para ella se toca su nombre
    private void pickSource(int out) {
        AlertDialog dlg = new AlertDialog.Builder(this)
                .setTitle(OUT_NAMES[out] + ": pulsa un boton del mando o elige")
                .setItems(SRC_NAMES, (d, which) -> {
                    padMap[out] = which;
                    showMap();
                }).create();
        dlg.setOnKeyListener((d, code, ev) -> {
            if (ev.getAction() != KeyEvent.ACTION_DOWN || ev.getRepeatCount() != 0) return false;
            int slot = slotOfKey(code);
            if (slot < 0 || (slot >= 8 && slot <= 11)) return false;
            padMap[out] = slot;
            showMap();
            d.dismiss();
            return true;
        });
        dlg.show();
        dlg.getWindow().getDecorView().setOnGenericMotionListener((v, ev) -> {
            if ((ev.getSource() & android.view.InputDevice.SOURCE_JOYSTICK) == 0) return false;
            float l = Math.max(ev.getAxisValue(MotionEvent.AXIS_LTRIGGER), ev.getAxisValue(MotionEvent.AXIS_BRAKE));
            float r = Math.max(ev.getAxisValue(MotionEvent.AXIS_RTRIGGER), ev.getAxisValue(MotionEvent.AXIS_GAS));
            int slot = l > 0.7f ? 6 : r > 0.7f ? 7 : -1;
            if (slot < 0) return false;
            padMap[out] = slot;
            showMap();
            dlg.dismiss();
            return true;
        });
    }

    private int dp(int v) { return (int) (v * getResources().getDisplayMetrics().density + 0.5f); }

    private GradientDrawable box(int fill, int stroke, int radiusDp) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(dp(radiusDp));
        if (stroke != 0) g.setStroke(dp(1), stroke);
        return g;
    }

    private Button button(String text, int fill, int textColor) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextColor(textColor);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(box(fill, GOLD, 10));
        b.setMinHeight(dp(48));
        b.setMinimumHeight(dp(48));
        return b;
    }

    private CheckBox check(String text, boolean on) {
        CheckBox c = new CheckBox(this);
        c.setText(text);
        c.setTextColor(TEXT);
        c.setTextSize(15);
        c.setChecked(on);
        c.setButtonTintList(ColorStateList.valueOf(GOLD));
        c.setPadding(dp(8), dp(10), dp(8), dp(10));
        return c;
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        int pad = dp(20);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(BG);
        scroll.setFillViewport(true);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        scroll.addView(root, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));

        // un panel de ancho limitado, centrado: en horizontal no se estira de borde a borde
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(box(PANEL, PANEL_LINE, 16));
        card.setPadding(dp(20), dp(18), dp(20), dp(18));
        LinearLayout.LayoutParams cardLp = new LinearLayout.LayoutParams(
                Math.min(dp(560), getResources().getDisplayMetrics().widthPixels - 2 * pad),
                ViewGroup.LayoutParams.WRAP_CONTENT);
        root.addView(card, cardLp);

        TextView title = new TextView(this);
        title.setText("CASTLEVANIA");
        title.setTextColor(GOLD);
        title.setTextSize(26);
        title.setTypeface(Typeface.create(Typeface.SERIF, Typeface.BOLD));
        title.setLetterSpacing(0.12f);
        title.setGravity(Gravity.CENTER);
        card.addView(title);
        TextView sub = new TextView(this);
        sub.setText("Curse of Darkness");
        sub.setTextColor(TEXT);
        sub.setTextSize(15);
        sub.setGravity(Gravity.CENTER);
        sub.setPadding(0, 0, 0, dp(14));
        card.addView(sub);

        TextView label = new TextView(this);
        label.setText("Ruta del juego");
        label.setTextColor(MUTED);
        label.setTextSize(13);
        card.addView(label);
        TextView hint = new TextView(this);
        hint.setText("Un .iso (se lee donde esta, sin copiarlo) o una carpeta con default.xbe (se copia dentro de la app)");
        hint.setTextColor(MUTED);
        hint.setTextSize(12);
        hint.setPadding(0, 0, 0, dp(6));
        card.addView(hint);

        path = new EditText(this);
        path.setSingleLine(true);
        path.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        path.setTextColor(TEXT);
        path.setHintTextColor(MUTED);
        path.setHint("Elige un ISO o una carpeta con los botones de abajo");
        path.setBackground(box(Color.parseColor("#120D17"), PANEL_LINE, 10));
        path.setPadding(dp(12), dp(12), dp(12), dp(12));
        path.setText(readSaved("game_path"));
        card.addView(path);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(0, dp(10), 0, dp(4));
        Button folder = button("Carpeta...", PANEL, TEXT);
        folder.setOnClickListener(v -> startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), PICK_FOLDER));
        Button iso = button("ISO...", PANEL, TEXT);
        iso.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            // el permiso tiene que sobrevivir a esta pantalla: el juego abre el .iso despues, por este URI
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(i, PICK_ISO);
        });
        LinearLayout.LayoutParams half = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        half.setMargins(0, 0, dp(6), 0);
        row.addView(folder, half);
        LinearLayout.LayoutParams half2 = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        half2.setMargins(dp(6), 0, 0, 0);
        row.addView(iso, half2);
        card.addView(row);

        TextView langLabel = new TextView(this);
        langLabel.setText("Idioma del juego (como el idioma de la consola)");
        langLabel.setTextColor(MUTED);
        langLabel.setTextSize(13);
        langLabel.setPadding(0, dp(12), 0, dp(6));
        card.addView(langLabel);
        try {
            language = Integer.parseInt(readSaved("console_language"));
        } catch (Exception ignored) {}
        languageButton = button("", PANEL, TEXT);
        languageButton.setOnClickListener(v -> new AlertDialog.Builder(this)
                .setTitle("Idioma")
                .setItems(LANG_NAMES, (d, which) -> {
                    language = LANG_CODES[which];
                    showLanguage();
                }).show());
        showLanguage();
        card.addView(languageButton);

        fps = check("Mostrar FPS en el juego", "1".equals(readSaved("fps_counter")));
        card.addView(fps);
        resolution = check("Mostrar resolucion real en el juego", "1".equals(readSaved("show_resolution")));
        card.addView(resolution);

        // saves: copia en la carpeta Castlevania, elegida con el selector (sin acceso a todos los archivos)
        TextView savesLabel = new TextView(this);
        savesLabel.setText("Copia de los saves");
        savesLabel.setTextColor(MUTED);
        savesLabel.setTextSize(13);
        savesLabel.setPadding(0, dp(12), 0, dp(2));
        card.addView(savesLabel);
        savesInfo = new TextView(this);
        savesInfo.setTextColor(TEXT);
        savesInfo.setTextSize(12);
        savesInfo.setPadding(0, 0, 0, dp(6));
        card.addView(savesInfo);
        LinearLayout savesRow = new LinearLayout(this);
        savesRow.setOrientation(LinearLayout.HORIZONTAL);
        savesFolderButton = button("Carpeta de saves...", PANEL, TEXT);
        savesFolderButton.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            // el selector se abre ya en Castlevania; en Android 11+ no deja elegir la raiz, pero si esta carpeta
            i.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                    DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "primary:Castlevania"));
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(i, PICK_SAVES);
        });
        restoreButton = button("Restaurar saves", PANEL, TEXT);
        restoreButton.setOnClickListener(v -> confirmRestore());
        LinearLayout.LayoutParams sHalf = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        sHalf.setMargins(0, 0, dp(6), 0);
        savesRow.addView(savesFolderButton, sHalf);
        LinearLayout.LayoutParams sHalf2 = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        sHalf2.setMargins(dp(6), 0, 0, 0);
        savesRow.addView(restoreButton, sHalf2);
        card.addView(savesRow);
        showSaves();

        loadMap();
        LinearLayout mapBox = new LinearLayout(this);
        mapBox.setOrientation(LinearLayout.VERTICAL);
        mapBox.setVisibility(View.GONE);
        TextView mapHint = new TextView(this);
        mapHint.setText("Cada fila es un boton de la Xbox; el boton de la derecha es el del mando que lo activa. "
                + "Toca la fila y pulsa el boton en tu mando, o elige de la lista.");
        mapHint.setTextColor(MUTED);
        mapHint.setTextSize(12);
        mapHint.setPadding(0, dp(4), 0, dp(8));
        mapBox.addView(mapHint);
        for (int i = 0; i < 16; i++) {
            LinearLayout line = new LinearLayout(this);
            line.setOrientation(LinearLayout.HORIZONTAL);
            line.setGravity(Gravity.CENTER_VERTICAL);
            TextView name = new TextView(this);
            name.setText(OUT_NAMES[i]);
            name.setTextColor(TEXT);
            name.setTextSize(14);
            line.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
            Button src = button("", PANEL, TEXT);
            final int out = i;
            src.setOnClickListener(v -> pickSource(out));
            mapButtons[i] = src;
            line.addView(src, new LinearLayout.LayoutParams(dp(150), ViewGroup.LayoutParams.WRAP_CONTENT));
            mapBox.addView(line);
        }
        Button reset = button("Restablecer mapeo", PANEL, TEXT);
        reset.setOnClickListener(v -> {
            for (int i = 0; i < 16; i++) padMap[i] = i;
            showMap();
        });
        LinearLayout.LayoutParams resetLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        resetLp.setMargins(0, dp(8), 0, 0);
        mapBox.addView(reset, resetLp);
        showMap();
        Button mapToggle = button("Mapeo de botones del mando", PANEL, TEXT);
        mapToggle.setOnClickListener(v ->
                mapBox.setVisibility(mapBox.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE));
        LinearLayout.LayoutParams toggleLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        toggleLp.setMargins(0, dp(10), 0, 0);
        card.addView(mapToggle, toggleLp);
        card.addView(mapBox);

        Button play = button("JUGAR", CRIMSON, Color.WHITE);
        play.setTextSize(17);
        playButton = play;
        play.setOnClickListener(v -> play());
        LinearLayout.LayoutParams playLp = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        playLp.setMargins(0, dp(14), 0, 0);
        card.addView(play, playLp);

        status = new TextView(this);
        status.setTextColor(Color.parseColor("#E8A0A8"));
        status.setTextSize(13);
        status.setGravity(Gravity.START);
        status.setPadding(0, dp(10), 0, 0);
        card.addView(status);

        // respeta la muesca y los bordes redondeados de la pantalla (y las barras del sistema)
        scroll.setOnApplyWindowInsetsListener((v, insets) -> {
            int l, t, r, bt;
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets i = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                l = i.left; t = i.top; r = i.right; bt = i.bottom;
            } else {
                l = insets.getSystemWindowInsetLeft(); t = insets.getSystemWindowInsetTop();
                r = insets.getSystemWindowInsetRight(); bt = insets.getSystemWindowInsetBottom();
            }
            root.setPadding(pad + l, pad + t, pad + r, pad + bt);
            return insets;
        });
        root.setPadding(pad, pad, pad, pad);
        setContentView(scroll);
        scroll.requestApplyInsets();
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        if (request == PICK_SAVES) {
            try {
                SavesSync.setTree(this, uri);
            } catch (Exception e) {
                status.setText("No pude conservar el acceso a esa carpeta: " + e.getMessage());
                return;
            }
            status.setText("");
            showSaves();
            return;
        }
        if (request == PICK_FOLDER) {
            importFolder(uri);
            return;
        }
        try {
            // se queda este permiso y se sueltan los de ISOs elegidos antes (Android limita cuantos se guardan)
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            for (android.content.UriPermission old : getContentResolver().getPersistedUriPermissions())
                if (!old.getUri().equals(uri) && !SavesSync.isSavesTree(this, old.getUri()))
                    getContentResolver().releasePersistableUriPermission(old.getUri(), Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception e) {
            status.setText("No pude conservar el acceso a ese archivo: " + e.getMessage());
            return;
        }
        path.setText(uri.toString());
        status.setText("");
    }

    private volatile boolean cancelImport;

    private void showSaves() {
        Uri tree = SavesSync.tree(this);
        if (tree == null) {
            savesInfo.setText("Elige la carpeta Castlevania una vez: ahi se guarda la copia de tus saves (sin dar acceso "
                    + "a todos los archivos). Tras reinstalar la app, vuelve a elegirla y pulsa Restaurar saves.");
            savesInfo.setTextColor(MUTED);
        } else {
            savesInfo.setText("Carpeta: " + SavesSync.treeName(tree) + "/saves_backup");
            savesInfo.setTextColor(TEXT);
        }
    }

    private void confirmRestore() {
        if (SavesSync.tree(this) == null) {
            status.setText("Primero elige la carpeta Castlevania con \"Carpeta de saves...\".");
            return;
        }
        new AlertDialog.Builder(this)
                .setTitle("Restaurar saves")
                .setMessage("La copia reemplaza tu save actual. El actual se guarda antes en "
                        + "Android/data/" + getPackageName() + "/files/hdd_before_restore. "
                        + "Despues cierra el juego del todo y abrelo otra vez.")
                .setNegativeButton("Cancelar", null)
                .setPositiveButton("Restaurar", (d, w) -> {
                    restoreButton.setEnabled(false);
                    status.setText("Restaurando...");
                    new Thread(() -> {
                        int n = SavesSync.restoreNow(getApplicationContext());
                        runOnUiThread(() -> {
                            restoreButton.setEnabled(true);
                            if (n >= 0) status.setText("Listo: " + n + " archivos restaurados. Cierra el juego del todo "
                                    + "y abrelo otra vez.");
                            else if (n == -1) status.setText("No hay copia en la carpeta elegida (busco saves_backup).");
                            else status.setText("No pude preparar la carpeta de saves del juego.");
                        });
                    }).start();
                }).show();
    }

    // una carpeta con default.xbe se copia a getExternalFilesDir()/assets, donde el juego la busca: el selector da un
    // URI y no una ruta, y leer una ruta fuera de la app pide el acceso a todos los archivos
    private void importFolder(Uri tree) {
        TextView message = new TextView(this);
        message.setText("Preparando...");
        message.setTextSize(14);
        message.setPadding(dp(24), dp(16), dp(24), dp(8));
        cancelImport = false;
        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle("Copiando el juego")
                .setView(message)
                .setCancelable(false)
                .setNegativeButton("Cancelar", (d, w) -> cancelImport = true)
                .create();
        dialog.show();
        final File dest = new File(getExternalFilesDir(null), "assets");
        final long[] copied = {0};
        new Thread(() -> {
            String error = null;
            try {
                String root = DocumentsContract.getTreeDocumentId(tree);
                if (!hasXbe(tree, root)) {
                    error = "esa carpeta no tiene default.xbe (si tienes un .iso, usa el boton ISO)";
                } else {
                    dest.mkdirs();
                    copyTree(tree, root, dest, dest.getCanonicalPath(), copied, message);
                    if (cancelImport) error = "copia cancelada";
                }
            } catch (Exception e) {
                error = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String problem = error;
            runOnUiThread(() -> {
                dialog.dismiss();
                if (problem == null) {
                    path.setText(dest.getPath());
                    status.setText("Listo: " + (copied[0] >> 20) + " MB copiados dentro de la app.");
                } else {
                    status.setText("No se pudo copiar el juego: " + problem);
                }
            });
        }).start();
    }

    private boolean hasXbe(Uri tree, String parentId) {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        try (android.database.Cursor c = getContentResolver().query(children,
                new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            while (c != null && c.moveToNext())
                if ("default.xbe".equalsIgnoreCase(c.getString(0))) return true;
        }
        return false;
    }

    private void copyTree(Uri tree, String parentId, File dir, String rootPath, long[] copied, TextView message)
            throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        java.util.ArrayList<String[]> entries = new java.util.ArrayList<>();
        try (android.database.Cursor c = getContentResolver().query(children, new String[]{
                DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null)) {
            while (c != null && c.moveToNext()) entries.add(new String[]{c.getString(0), c.getString(1), c.getString(2)});
        }
        for (String[] e : entries) {
            if (cancelImport) return;
            if (e[1] == null || e[1].contains("/") || e[1].equals(".") || e[1].equals("..")) continue;
            File out = new File(dir, e[1]);
            // el nombre lo da el proveedor de archivos: no se confia en que se quede dentro de la carpeta
            if (!out.getCanonicalPath().startsWith(rootPath + File.separator)) continue;
            if (DocumentsContract.Document.MIME_TYPE_DIR.equals(e[2])) {
                out.mkdirs();
                copyTree(tree, e[0], out, rootPath, copied, message);
            } else {
                copyFile(DocumentsContract.buildDocumentUriUsingTree(tree, e[0]), out, e[1], copied, message);
            }
        }
    }

    private void copyFile(Uri from, File out, String name, long[] copied, TextView message) throws IOException {
        long lastShown = 0;
        try (InputStream in = getContentResolver().openInputStream(from); OutputStream os = new FileOutputStream(out)) {
            if (in == null) throw new IOException("no pude leer " + name);
            byte[] buf = new byte[1 << 20];
            int n;
            while (!cancelImport && (n = in.read(buf)) != -1) {
                os.write(buf, 0, n);
                copied[0] += n;
                long now = android.os.SystemClock.uptimeMillis();
                if (now - lastShown > 300) {
                    lastShown = now;
                    final String text = name + "\n" + (copied[0] >> 20) + " MB copiados";
                    runOnUiThread(() -> message.setText(text));
                }
            }
        }
        if (cancelImport) out.delete();
    }

    private File file() { return new File(getExternalFilesDir(null), "launcher.txt"); }
    private File lastFile() { return new File(getExternalFilesDir(null), "launcher_last.txt"); }

    // launcher.txt si el juego aun no lo ha leido; si no, la copia que deja el codigo nativo al borrarlo
    private String readSaved(String key) {
        for (File f : new File[]{file(), lastFile()}) {
            try {
                for (String line : new String(java.nio.file.Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8).split("\n"))
                    if (line.startsWith(key + "=")) return line.substring(key.length() + 1).trim();
            } catch (Exception ignored) {}
        }
        return "";
    }

    // una carpeta sirve si tiene default.xbe (archivos extraidos) o una imagen .iso, como busca el codigo nativo
    private static boolean hasGame(File dir) {
        if (new File(dir, "default.xbe").exists()) return true;
        File[] list = dir.listFiles();
        if (list != null)
            for (File x : list)
                if (x.isFile() && x.getName().toLowerCase().endsWith(".iso")) return true;
        return false;
    }

    // null si la ruta sirve; si no, lo que le pasa
    private String problemWith(String p) {
        if (p.isEmpty()) return "elige un ISO o una carpeta con los botones";
        if (p.startsWith("content://")) {
            try (android.os.ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(Uri.parse(p), "r")) {
                return pfd == null ? "no se pudo abrir el archivo" : null;
            } catch (Exception e) {
                return "ya no tengo acceso a ese archivo, elige el ISO otra vez (" + e.getMessage() + ")";
            }
        }
        File f = new File(p);
        boolean isFile = f.isFile(), isDir = f.isDirectory();
        boolean ok = isDir ? hasGame(f) : isFile && p.toLowerCase().matches(".*\\.(iso|xbe)");
        if (ok) return null;
        if (!f.exists()) return "esa ruta no existe o la app no puede leerla: usa los botones ISO o Carpeta";
        if (isDir) return "la carpeta no tiene default.xbe ni ningun .iso";
        return "el archivo no es .iso ni .xbe";
    }

    private void play() {
        String p = path.getText().toString().trim();
        String problem = problemWith(p);
        if (problem != null) {
            status.setText("Ruta no valida: " + problem + "\n" + p);
            return;
        }
        try (FileOutputStream out = new FileOutputStream(file())) {
            String text = "game_path=" + p + "\nfps_counter=" + (fps.isChecked() ? 1 : 0)
                    + "\nshow_resolution=" + (resolution.isChecked() ? 1 : 0)
                    + "\nconsole_language=" + language
                    + "\npad_map=" + mapText() + "\n";
            out.write(text.getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            status.setText("No pude guardar: " + e.getMessage());
            return;
        }
        // antes de abrir el juego: si su carpeta de saves esta vacia o le faltan archivos, se traen de la copia
        playButton.setEnabled(false);
        status.setText("Buscando saves...");
        new Thread(() -> {
            SavesSync.restoreMissing(getApplicationContext());
            runOnUiThread(() -> {
                Intent i = new Intent();
                i.setClassName(getPackageName(), GAME_ACTIVITY);
                startActivity(i);
                finish();
            });
        }).start();
    }
}
