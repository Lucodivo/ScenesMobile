package com.inasweaterpoorlyknit.scenes.graphics.scenes

import android.content.res.Resources
import android.opengl.GLES32.*
import android.view.MotionEvent
import com.inasweaterpoorlyknit.noopmath.Vec2
import com.inasweaterpoorlyknit.noopmath.Vec3
import com.inasweaterpoorlyknit.noopmath.abs
import com.inasweaterpoorlyknit.noopmath.max
import com.inasweaterpoorlyknit.noopmath.min
import com.inasweaterpoorlyknit.noopmath.mod
import com.inasweaterpoorlyknit.scenes.*
import com.inasweaterpoorlyknit.scenes.common.clamp
import com.inasweaterpoorlyknit.scenes.graphics.*
import com.inasweaterpoorlyknit.scenes.repositories.UserPreferencesDataStoreRepository
import kotlinx.coroutines.flow.firstOrNull
import kotlinx.coroutines.runBlocking
import java.nio.IntBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

data class Resolution (
    val width: Int,
    val height: Int
)

class MengerPrisonScene(private val rotationSensorHelper: RotationSensorHelper, private val userPreferencesRepo: UserPreferencesDataStoreRepository, private val resources: Resources, private val startingOrientation: Orientation) : Scene() {

    companion object {
        // TODO: Consider linking box/container dimen to uniform?
        const val REPEATED_CONTAINER_DIMEN = 40.0f

        private object UniformNames {
            const val ITERATIONS = "iterations"
            const val VIEW_PORT_RESOLUTION = "viewPortResolution"
            const val RAY_ORIGIN = "rayOrigin"
            const val CAMERA_ROTATION_MAT = "cameraRotationMat"
        }

        val resolutionFactorOptions = arrayOf(
            1.0f/32.0f,
            1.0f/16.0f,
            1.0f/8.0f,
            1.0f/4.0f,
            1.0f/2.0f,
            1.0f,
        )
        const val DEFAULT_RESOLUTION_INDEX = 3

        private const val MAX_ITERATIONS = 5
        private val defaultCameraForward = Vec3(0.0f, 0.0f, 1.0f)
        private const val CAMERA_SPEED_NORMAL = 0.5f
        private const val CAMERA_SPEED_FAST = 1.5f
    }

    private lateinit var mengerPrisonProgram: Program
    private var quadVAO: Int = -1
    private var offscreenFramebuffer = FrameBuffer()

    private lateinit var resolutions: Array<Resolution>
    private var currentResolutionIndex: Int = DEFAULT_RESOLUTION_INDEX
    private var prevFrameResolutionIndex: Int = DEFAULT_RESOLUTION_INDEX // informs us of a resolution change within onDrawFrame()
    private val resolution get() = resolutions[currentResolutionIndex]

    private var cameraPos = Vec3(0.0f, 0.0f, 0.0f)
    private var cameraForward = defaultCameraForward

    private var elapsedTime : Double = 0.0
    private var lastFrameTime: Double = 0.0
    private var firstFrameTime: Double = -1.0

    private var actionDown = false

    init {
        // TODO: Don't user runBlocking
        val userPrefMengerIndex = runBlocking { userPreferencesRepo.mengerIndex.firstOrNull() }
        userPrefMengerIndex?.let { index ->
            prevFrameResolutionIndex = currentResolutionIndex
            currentResolutionIndex = clamp(index, 0, resolutionFactorOptions.size - 1)
        }
    }

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        mengerPrisonProgram = Program(resources, R.raw.uv_coord_vertex_shader, R.raw.menger_prison_fragment_shader)

        // setup vertex attributes for quad
        val quadVAOBuffer = IntBuffer.allocate(1)
        val quadVBOBuffer = IntBuffer.allocate(1)
        val quadEBOBuffer = IntBuffer.allocate(1)
        initializeFrameBufferQuadVertexAttBuffers(quadVAOBuffer, quadVBOBuffer, quadEBOBuffer)
        quadVAO = quadVAOBuffer[0]
        glBindVertexArray(quadVAO)

        firstFrameTime = systemTimeInDeciseconds()
        glClearColor(Vec3(0.5f, 0.0f, 0.0f))

        mengerPrisonProgram.use()
        mengerPrisonProgram.setUniform(UniformNames.ITERATIONS, MAX_ITERATIONS)
    }

    override fun onSurfaceChanged(gl: GL10, width: Int, height: Int) {
        super.onSurfaceChanged(gl, width, height)

        // surface resolution has changed, adjust accordingly
        initResolutions(width, height)
        offscreenFramebuffer.delete()
        offscreenFramebuffer = initializeFrameBuffer(resolution.width, resolution.height)

        mengerPrisonProgram.use()
        mengerPrisonProgram.setUniform(UniformNames.VIEW_PORT_RESOLUTION, resolution.width.toFloat(), resolution.height.toFloat())
    }

    private fun initResolutions(width: Int, height: Int) {
        resolutions = Array(resolutionFactorOptions.size) { i ->
            val resFactor = resolutionFactorOptions[i]
            Resolution((width * resFactor).toInt(), (height * resFactor).toInt())
        }
    }

    private fun resetScene() {
        cameraPos = Vec3(0.0f, 0.0f, 0.0f)
        cameraForward = defaultCameraForward
        elapsedTime = 0.0
        lastFrameTime = 0.0
        firstFrameTime = systemTimeInDeciseconds()
        rotationSensorHelper.reset()
    }

    override fun onDrawFrame(gl: GL10?) {
        // NOTE: OpenGL calls must be called within specified call back functions
        // Calling OpenGL functions in other functions will surely result in bugs
        elapsedTime = systemTimeInDeciseconds() - firstFrameTime
        val deltaTime = elapsedTime - lastFrameTime
        lastFrameTime = elapsedTime

        // Update scene
        val rotationMat = rotationSensorHelper.getRotationMatrix(startingOrientation)
        cameraForward = rotationMat * defaultCameraForward
        val cameraSpeed = if(actionDown) CAMERA_SPEED_FAST else CAMERA_SPEED_NORMAL
        cameraPos += cameraForward * cameraSpeed * deltaTime.toFloat()
        // prevent floating point values from growing to unreasonable values
        cameraPos %= REPEATED_CONTAINER_DIMEN

        // check for collision
        val cameraDistToPrison = sdMengerPrison(cameraPos)
        if(cameraDistToPrison <= 0.0f) {
            // reset scene
            resetScene()
        }

        if(prevFrameResolutionIndex != currentResolutionIndex) {
            // new resolution index changed by user, adjust accordingly
            offscreenFramebuffer.delete()
            offscreenFramebuffer = initializeFrameBuffer(resolution.width, resolution.height)
            prevFrameResolutionIndex = currentResolutionIndex
            mengerPrisonProgram.setUniform(UniformNames.VIEW_PORT_RESOLUTION, resolution.width.toFloat(), resolution.height.toFloat())
        }

        // draw to offscreen framebuffer
        glViewport(0, 0, resolution.width, resolution.height)
        glBindFramebuffer(GL_FRAMEBUFFER, offscreenFramebuffer.index)
        glClear(GL_COLOR_BUFFER_BIT)

        mengerPrisonProgram.use()
        mengerPrisonProgram.setUniform(UniformNames.RAY_ORIGIN, cameraPos)
        mengerPrisonProgram.setUniform(UniformNames.CAMERA_ROTATION_MAT, rotationMat)
        glDrawElements(GL_TRIANGLES, frameBufferQuadNumVertices, GL_UNSIGNED_INT, 0 /* offset in the EBO */)

        // blit rendered image onto onscreen buffer, scaling with "blocky"/"point" sampling where needed
        glViewport(0, 0, windowWidth, windowHeight)
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, offscreenFramebuffer.index)
        glBlitFramebuffer(0, 0, resolution.width, resolution.height, 0, 0, windowWidth, windowHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST)
    }

    override fun onAttach() {
        rotationSensorHelper.init()
    }

    override fun onDetach() {
        rotationSensorHelper.init()
    }

    override fun onTouchEvent(event: MotionEvent) = when (event.action) {
        MotionEvent.ACTION_DOWN -> actionDown = true
        MotionEvent.ACTION_UP -> actionDown = false
        else -> {}
    }

    // below is code pulled from fragment shader to verify if camera has collided with the structure
    private fun sdMengerPrison(pos: Vec3): Float {
        val boxDimen = 20.0f
        val halfBoxDimen = boxDimen * 0.5f
        val hitDist = 0.01f
        var prisonRay: Vec3 = mod(pos, boxDimen * 2.0f)
        prisonRay -= Vec3(boxDimen, boxDimen, boxDimen) // move container origin to center
        var mengerPrisonDist = sdCross(prisonRay, Vec3(halfBoxDimen))
        if (mengerPrisonDist > hitDist) return mengerPrisonDist // use dist of biggest crosses as bounding volume
        var scale = 1.0f
        for (i in 0 until MAX_ITERATIONS) {
            val boxedWorldDimen: Float = boxDimen / scale
            val posShiftVal = boxedWorldDimen * 0.5f
            val posShift = Vec3(posShiftVal, posShiftVal, posShiftVal)
            var ray: Vec3 = mod(pos + posShift, boxedWorldDimen)
            ray -= posShift
            ray *= scale
            var crossesDist = sdCross(ray * 3.0f, Vec3(halfBoxDimen))
            scale *= 3.0f
            crossesDist /= scale
            mengerPrisonDist = max(mengerPrisonDist, -crossesDist)
        }
        return mengerPrisonDist
    }

    private fun sdRect(pos: Vec2, dimen: Vec2): Float {
        val rayToCorner: Vec2 = abs(pos) - dimen
        val maxDelta: Float = min(max(rayToCorner.x, rayToCorner.y), 0.0f)
        return max(rayToCorner, Vec2(0.0f, 0.0f)).len + maxDelta
    }

    private fun sdCross(pos: Vec3, dimen: Vec3): Float {
        val distA = sdRect(pos.xy, dimen.xy)
        val distB = sdRect(pos.xz, dimen.xz)
        val distC = sdRect(pos.yz, dimen.yz)
        return min(distA, min(distB, distC))
    }
}