package com.weikun.videodemo;

import android.os.Bundle;
import android.app.Activity;
import android.util.Log;
import android.view.Menu;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;

public class MainActivity extends Activity implements SurfaceHolder.Callback {

	private static final String TAG = MainActivity.class.getSimpleName();

	private static native int nativeInit();

	private static native void nativeSurfaceInit(Object surface);

	private static native void nativeSurfaceFinalize();

	private static native void nativeVideoPlay();
	
	private static native void nativeVideoStop();
	
	static {
		System.loadLibrary("SDL");
		System.loadLibrary("ffmpegutils");
	}

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);

		findViewById(R.id.btn_play).setOnClickListener(new View.OnClickListener() {
			
			@Override
			public void onClick(View v) {
				nativeVideoPlay();
			}
		});
		
		findViewById(R.id.btn_stop).setOnClickListener(new View.OnClickListener() {
			
			@Override
			public void onClick(View v) {
				nativeVideoStop();
			}
		});
		
		SurfaceView sv = (SurfaceView) this.findViewById(R.id.video_view);
		SurfaceHolder sh = sv.getHolder();
		sh.addCallback(this);

	}

	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		// Inflate the menu; this adds items to the action bar if it is present.
		getMenuInflater().inflate(R.menu.main, menu);
		return true;
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width,
			int height) {
		Log.i(TAG, "surfaceChanged");
		nativeSurfaceInit(holder.getSurface());
		
		int init = nativeInit();
		Log.i(TAG, "init result " + init);
	}

	@Override
	public void surfaceCreated(SurfaceHolder holder) {

	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		Log.i(TAG, "surfaceDestroyed");
		nativeSurfaceFinalize();
	}

}
