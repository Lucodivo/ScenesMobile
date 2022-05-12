package com.inasweaterpoorlyknit.learnopengl_androidport.utils

import android.os.Build.VERSION.SDK_INT
import android.view.View
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat

fun systemTimeInSeconds(): Double {
  return System.nanoTime().toDouble() / 1000000000
}

fun systemTimeInDeciseconds(): Double {
  // note: time measured in deciseconds (10^-1 seconds)
  return System.nanoTime().toDouble() / 100000000
}

fun androidx.appcompat.app.AppCompatActivity.hideSystemUI() {
  if(SDK_INT >= 30) {
    window.setDecorFitsSystemWindows(false)
    WindowInsetsControllerCompat(window, window.decorView).let { controller ->
      controller.hide(WindowInsetsCompat.Type.systemBars())
      controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }
  } else { // TODO: System visibility is deprecated, remove when minSDK is 30
    window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or // hide the navigation
                    View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or // lay out view as if the navigation will be hidden
                    View.SYSTEM_UI_FLAG_IMMERSIVE or // used with HIDE_NAVIGATION to remain interactive when hiding navigation
                    View.SYSTEM_UI_FLAG_FULLSCREEN or // fullscreen
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or // lay out view as if fullscreen
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE) // stable view of content (layout view size doesn't change)
  }
}