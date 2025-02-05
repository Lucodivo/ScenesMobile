package com.inasweaterpoorlyknit.scenes.graphics.scenes

import android.content.Context
import android.content.res.Resources
import android.opengl.GLES32.*
import android.util.Log
import android.view.GestureDetector
import android.view.GestureDetector.SimpleOnGestureListener
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import androidx.core.math.MathUtils.clamp
import com.inasweaterpoorlyknit.noopmath.Mat2
import com.inasweaterpoorlyknit.noopmath.Vec2
import com.inasweaterpoorlyknit.noopmath.Vec3
import com.inasweaterpoorlyknit.noopmath.dVec2
import com.inasweaterpoorlyknit.scenes.*
import com.inasweaterpoorlyknit.scenes.graphics.*
import com.inasweaterpoorlyknit.scenes.graphics.scenes.MengerPrisonScene.Companion.resolutionFactorOptions
import com.inasweaterpoorlyknit.scenes.repositories.UserPreferencesDataStoreRepository
import kotlinx.coroutines.flow.firstOrNull
import kotlinx.coroutines.runBlocking
import java.nio.IntBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.min
import kotlin.math.pow

class MandelbrotScene(context: Context, userPreferencesRepo: UserPreferencesDataStoreRepository, private val resources: Resources) : Scene() {

    companion object {
        private object UniformNames {
            const val VIEW_PORT_RESOLUTION = "viewPortResolution"
            const val ACCENT_COLOR = "accentColor"
            const val ZOOM = "zoom"
            const val CENTER_OFFSET = "centerOffset"
            const val ROTATION_MAT = "rotationMat"
        }

        data class AccentColor(val name: String, val accentColor: Vec3)
        val colors = arrayOf(
            AccentColor("Red", Vec3(1.0f, 0.0f, 0.0f)), // blue/yellow
            AccentColor("Green", Vec3(0.0f, 1.0f, 0.0f)), // pastel blue/pink
            AccentColor("Blue", Vec3(0.0f, 0.0f, 1.0f)), // pink/purple
        )
        const val DEFAULT_COLOR_INDEX = 0

        private const val MIN_ZOOM: Double = .25 // 1 means that we can see -0.5 to 0.5 in the minimum dimension
        private const val MAX_ZOOM: Double = 130000.0 // TODO: Expand max if zoom is every expanded beyond current capabilities
        private const val BASE_ZOOM: Double = MIN_ZOOM
    }

    object frameBuffer {
        var id = -1
        var colorAttachmentId = -1
        var width = -1
        var height = -1
    }

    private lateinit var mandelbrotProgram: Program
    private var quadVAO: Int = -1

    private var zoom = BASE_ZOOM
    private var centerOffset = dVec2(0.0, 0.0) //Vec2HighP(-1.70, 0.0)
    private var frameRotationMatrix = Mat2(1f)
    private var inputSinceLastDraw = true

    // Motion event variables
    private var postPinchZoom_panFlushRequired = false
    private var prevScaleGestureFocus: Vec2 = Vec2(0f, 0f)
    private var previousX: Float = 0f
    private var previousY: Float = 0f
    private var doubleTapInProgress = false

    private var accentColorsIndex: Int = DEFAULT_COLOR_INDEX
    private var pixelsPerUnit: Double = 0.0

    private fun scaleZoom(factor: Float) { zoom = clamp(zoom * factor, MIN_ZOOM, MAX_ZOOM) }

    // TODO: Consider handling all scenarios through custom RotateGestureDetector?
    private val scaleGestureDetector: ScaleGestureDetector = ScaleGestureDetector(context, object : ScaleGestureDetector.OnScaleGestureListener {
        override fun onScale(detector: ScaleGestureDetector): Boolean {
            // zoom
            scaleZoom(detector.scaleFactor)

            // pan
            val dx: Double = detector.focusX.toDouble() - prevScaleGestureFocus.x.toDouble()
            val dy: Double = detector.focusY.toDouble() - prevScaleGestureFocus.y.toDouble()
            prevScaleGestureFocus = Vec2(detector.focusX, detector.focusY)
            pan(dx, dy)

            return true
        }
        override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
            prevScaleGestureFocus = Vec2(detector.focusX, detector.focusY)
            return true
        }
        override fun onScaleEnd(detector: ScaleGestureDetector) {
            postPinchZoom_panFlushRequired = true
        }
    }).also{ it.isQuickScaleEnabled = true }

    private var gestureDetector: GestureDetector = GestureDetector(context, object : SimpleOnGestureListener() {
        override fun onSingleTapConfirmed(e: MotionEvent): Boolean {
            doubleTapInProgress = false
            return true
        }
        override fun onDoubleTap(e: MotionEvent): Boolean {
            doubleTapInProgress = true
            return true
        }
        override fun onDoubleTapEvent(e: MotionEvent): Boolean {
            doubleTapInProgress = true
            return true
        }
    })
    private val rotateGestureDetector = RotateGestureDetector()

    init {
        // TODO: Don't use runBlocking
        val userPrefMandelIndex = runBlocking { userPreferencesRepo.mandelbrotIndex.firstOrNull() }
        userPrefMandelIndex?.let { index ->
            accentColorsIndex = clamp(index, 0, resolutionFactorOptions.size - 1)
        }
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        mandelbrotProgram = Program(resources, R.raw.mandelbrot_vertex_shader, R.raw.mandelbrot_fragment_shader)

        pixelsPerUnit = min(windowWidth, windowHeight).toDouble()

        // setup vertex attributes for quad
        val quadVAOBuffer = IntBuffer.allocate(1)
        val quadVBOBuffer = IntBuffer.allocate(1)
        val quadEBOBuffer = IntBuffer.allocate(1)
        initializeFrameBufferQuadVertexAttBuffers(quadVAOBuffer, quadVBOBuffer, quadEBOBuffer)
        quadVAO = quadVAOBuffer[0]

        glClearColor(Vec3(1f, 0f, 0f))

        mandelbrotProgram.use()
        glBindVertexArray(quadVAO)
        mandelbrotProgram.setUniform(UniformNames.VIEW_PORT_RESOLUTION, windowWidth.toFloat(), windowHeight.toFloat())
        // TODO: Only set uniform if there is a change
        mandelbrotProgram.setUniform(UniformNames.ACCENT_COLOR, colors[accentColorsIndex].accentColor)
    }

    override fun onSurfaceChanged(gl: GL10, width: Int, height: Int) {
        super.onSurfaceChanged(gl, width, height)
        inputSinceLastDraw = true;

        pixelsPerUnit = min(windowWidth, windowHeight).toDouble()

        mandelbrotProgram.use()
        mandelbrotProgram.setUniform(UniformNames.VIEW_PORT_RESOLUTION, width.toFloat(), height.toFloat())

        setupDrawBuffer()
    }

    private fun setupDrawBuffer(){
        val glInt = IntBuffer.allocate(1)
        if(frameBuffer.width != -1) {
            if(frameBuffer.width == windowWidth && frameBuffer.height == windowHeight) {
               return
            }

            glInt.put(0, frameBuffer.id)
            glDeleteFramebuffers(1, glInt)
            glInt.put(0, frameBuffer.colorAttachmentId)
            glDeleteTextures(1, glInt)
        }

        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, glInt)
        val originalDrawFramebuffer = glInt[0]
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, glInt)
        val originalReadFramebuffer = glInt[0]
        glGetIntegerv(GL_ACTIVE_TEXTURE, glInt)
        val originalActiveTexture = glInt[0]

        // creating frame buffer
        glGenFramebuffers(1, glInt)
        frameBuffer.id = glInt[0]
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer.id)

        // creating frame buffer color texture
        glGenTextures(1, glInt)
        frameBuffer.colorAttachmentId = glInt[0]
        // NOTE: Binding the texture to the GL_TEXTURE_2D target, means that
        // NOTE: gl operations on the GL_TEXTURE_2D target will affect our texture
        // NOTE: while it is remains bound to that target
        glActiveTexture(GL_TEXTURE0)
        glGetIntegerv(GL_TEXTURE_BINDING_2D, glInt)
        val originalTexture = glInt[0]

        glBindTexture(GL_TEXTURE_2D, frameBuffer.colorAttachmentId)
        glTexImage2D(GL_TEXTURE_2D, 0/*LoD*/, GL_RGB, windowWidth, windowHeight, 0/*border*/, GL_RGB, GL_UNSIGNED_BYTE, null)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR)

        // attach texture w/ color to frame buffer
        glFramebufferTexture2D(GL_FRAMEBUFFER, // frame buffer we're targeting (draw, read, or both)
            GL_COLOR_ATTACHMENT0, // type of attachment and index of attachment
            GL_TEXTURE_2D, // type of texture
            frameBuffer.colorAttachmentId, // texture
            0) // mipmap level

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            Log.e("Mandelbrot Scene Error: ", "Error creating framebuffer!")
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, originalDrawFramebuffer)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, originalReadFramebuffer)
        glBindTexture(GL_TEXTURE_2D, originalTexture) // re-bind original texture
        glActiveTexture(originalActiveTexture)
    }

    override fun onDrawFrame(gl: GL10?) {
        // NOTE: OpenGL calls must be called within specified call back functions
        // Calling OpenGL functions in other functions will result in bugs

        // The mandelbrot scene is unique in that it doesn't need to redraw if there are no changes in zoom, accent color index, center offset or rotation
        // This makes it a great candidate for preventing additional draws. However, we do not have control over swapping of buffers.
        // A potential way to optimize this would be to draw to a separate buffer that always gets copied to the display buffer but in which that separate buffer
        // is only updated when the changes noted above occur
        val glInt = IntBuffer.allocate(1)
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, glInt)
        val givenDrawFramebuffer = glInt[0]
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, glInt)
        val givenReadFramebuffer = glInt[0]

        if(inputSinceLastDraw) {
            val rotation = rotateGestureDetector.lifetimeRotation

            frameRotationMatrix = rotationMat2D(rotation)

            glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer.id)

            glClear(GL_COLOR_BUFFER_BIT)

            mandelbrotProgram.setUniform(UniformNames.ZOOM, zoom.toFloat())
            mandelbrotProgram.setUniform(UniformNames.CENTER_OFFSET, centerOffset.x.toFloat(), centerOffset.y.toFloat())
            mandelbrotProgram.setUniform(UniformNames.ROTATION_MAT, frameRotationMatrix)
            glDrawElements(GL_TRIANGLES, frameBufferQuadNumVertices, GL_UNSIGNED_INT, 0) // offset in the EBO

            inputSinceLastDraw = false
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer.id)
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, givenDrawFramebuffer)
        glBlitFramebuffer(0, 0, windowWidth, windowHeight, 0, 0, windowWidth, windowHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, givenReadFramebuffer)
    }


    private fun pan(deltaX: Double, deltaY: Double) {
        // y is negated because screen coordinates are positive going down
        var centerDelta = dVec2(deltaX / (zoom * pixelsPerUnit), -deltaY / (zoom * pixelsPerUnit))
        centerDelta = dVec2(frameRotationMatrix * centerDelta.toVec2())
        // the value is SUBTRACTED from the center/origin
        // This is because the center represents the center of the mandelbrot set NOT the center of the camera
        // Instead of moving the camera 2 units left, we move the mandelbrot set 2 units right and get the desired result
        centerOffset -= centerDelta

        centerOffset = dVec2(
            clamp(centerOffset.x, -2.0, 2.0),
            clamp(centerOffset.y, -2.0, 2.0)
        )
    }

    override fun onTouchEvent(event: MotionEvent) {
        inputSinceLastDraw = true

        rotateGestureDetector.onTouchEvent(event)
        scaleGestureDetector.onTouchEvent(event)
        gestureDetector.onTouchEvent(event)

        if(scaleGestureDetector.isInProgress || rotateGestureDetector.isActive) return

        // single pointer gesture events
        when (event.action) {
            MotionEvent.ACTION_DOWN -> {
                previousX = event.x
                previousY = event.y
            }
            MotionEvent.ACTION_MOVE -> {
                val dx: Double = event.x.toDouble() - previousX.toDouble()
                val dy: Double = event.y.toDouble() - previousY.toDouble()
                previousX = event.x
                previousY = event.y

                if(postPinchZoom_panFlushRequired) {
                    // When one finger is pulled off of a pinch to zoom, that pinch to zoom event ends but the single finger event continues.
                    // The initial result causes a MotionEvent with a huge delta position. This aims to ignore this MotionEvent.
                    postPinchZoom_panFlushRequired = false
                } else {
                    if(doubleTapInProgress) {
                        // normalize delta y to be between 0 to 2
                        val dyNormalized = (dy + windowHeight) / windowHeight
                        val factor = dyNormalized.pow(4)
                        scaleZoom(factor.toFloat())
                    } else {
                        pan(dx, dy)
                    }
                }
            }
            MotionEvent.ACTION_UP -> {
                doubleTapInProgress = false
            }
            else -> {}
        }
    }
}