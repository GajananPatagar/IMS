package com.ims.mixer;

import android.Manifest;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import java.io.File;
import java.io.RandomAccessFile;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import org.json.JSONArray;
import org.json.JSONObject;

public class MainActivity extends AppCompatActivity {
    private WebView webView;
    private MappedByteBuffer neuralink5BObbBuffer;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        webView = new WebView(this);
        setContentView(webView);

        WebSettings ws = webView.getSettings();
        ws.setJavaScriptEnabled(true);
        ws.setDomStorageEnabled(true);
        ws.setAllowFileAccess(true);
        ws.setAllowContentAccess(true);
        ws.setAllowUniversalAccessFromFileURLs(true);
        ws.setMediaPlaybackRequiresUserGesture(false);

        webView.setWebViewClient(new WebViewClient());
        webView.setWebChromeClient(new WebChromeClient());
        webView.addJavascriptInterface(new IMSNativeBridge(), "IMSNative");

        requestAudioPermissions();
        mountNeuralink1GBModelObb();

        webView.loadUrl("file:///android_asset/index.html");
    }

    private void requestAudioPermissions() {
        if (Build.VERSION.SDK_INT >= 33) {
            if (checkSelfPermission(Manifest.permission.READ_MEDIA_AUDIO) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.READ_MEDIA_AUDIO}, 101);
            }
        } else {
            if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.READ_EXTERNAL_STORAGE}, 101);
            }
        }
    }

    private void mountNeuralink1GBModelObb() {
        try {
            File obbDir = new File(Environment.getExternalStorageDirectory(), "Android/obb/com.ims.mixer");
            File obbFile = new File(obbDir, "main.16.com.ims.mixer.obb");
            if (obbFile.exists()) {
                RandomAccessFile raf = new RandomAccessFile(obbFile, "r");
                neuralink5BObbBuffer = raf.getChannel().map(FileChannel.MapMode.READ_ONLY, 0, Math.min(raf.length(), 1073741824L));
            }
        } catch (Exception ignored) {}
    }

    public class IMSNativeBridge {
        @JavascriptInterface
        public String scanOfflineDeviceSongs() {
            JSONArray songs = new JSONArray();
            try {
                Uri uri = MediaStore.Audio.Media.EXTERNAL_CONTENT_URI;
                String[] proj = { MediaStore.Audio.Media._ID, MediaStore.Audio.Media.TITLE, MediaStore.Audio.Media.ARTIST, MediaStore.Audio.Media.DATA, MediaStore.Audio.Media.DURATION };
                Cursor c = getContentResolver().query(uri, proj, MediaStore.Audio.Media.IS_MUSIC + " != 0", null, MediaStore.Audio.Media.TITLE + " ASC");
                if (c != null) {
                    while (c.moveToNext()) {
                        JSONObject o = new JSONObject();
                        o.put("id", c.getLong(0));
                        o.put("title", c.getString(1));
                        o.put("artist", c.getString(2));
                        o.put("path", "file://" + c.getString(3));
                        o.put("duration", c.getLong(4) / 1000.0);
                        songs.put(o);
                    }
                    c.close();
                }
            } catch (Exception ignored) {}
            return songs.toString();
        }

        @JavascriptInterface
        public boolean is1GBNeuralinkObbMounted() {
            return neuralink5BObbBuffer != null;
        }
    }
}
