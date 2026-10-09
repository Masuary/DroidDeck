package com.droiddeck.launcher.gpu

import android.content.Context
import android.os.Environment
import android.os.Process
import android.util.Log
import com.droiddeck.launcher.core.FileUtils
import com.droiddeck.launcher.core.HostProcess
import com.droiddeck.launcher.core.SessionPart
import java.io.File
import java.io.FileWriter
import java.io.PrintWriter

/**
 * Venus: the session's Vulkan on a GPU Turnip cannot drive (PowerVR, Mali).
 *
 * Turnip talks to an Adreno through KGSL, so on anything else the rootfs has no GPU at all and
 * gamescope stops at "failed to find physical device". Venus splits the driver in two instead:
 * Mesa's Venus ICD inside the rootfs serialises every Vulkan call over a unix socket (vtest), and
 * virglrenderer's vtest server out here, a bionic program under the app's uid, replays them on
 * Android's own libvulkan - the vendor driver, whatever the GPU is.
 *
 * The server ships as `libvenusserver.so` for the reason PulseAudio does: Android only executes
 * from the native library directory. The ICD (Arch Linux ARM's vulkan-virtio, glibc) ships as an
 * asset and is unpacked under the files directory, which the session binds at its own path, so
 * the manifest's absolute library path is valid on both sides. Built by tools/venus.
 */
object Venus {
    private const val TAG = "Venus"
    const val SERVER = "libvenusserver.so"
    private const val ASSET_DIR = "venus"
    private const val ICD_LIBRARY = "libvulkan_virtio.so"
    private const val MANIFEST = "venus_icd.json"
    /** "on" or "off" forces the choice, for a device that cannot be reached with a debugger. */
    private const val OVERRIDE_FILE = "Download/droiddeck-venus"

    fun dir(context: Context) = File(context.filesDir, "venus")

    /** Inside the files directory, so the server and the guest name it by the same path. */
    fun socket(context: Context) = File(dir(context), "vtest.sock")

    fun serverBinary(context: Context) = File(context.applicationInfo.nativeLibraryDir, SERVER)

    /** Venus for this session: forced by the override file, else whenever the GPU is not an Adreno. */
    fun wanted(context: Context): Boolean {
        val forced = File(Environment.getExternalStorageDirectory(), OVERRIDE_FILE)
            .takeIf { it.isFile }?.let { FileUtils.readString(it)?.trim()?.lowercase() }
        val wanted = when {
            forced?.startsWith("on") == true -> true
            forced?.startsWith("off") == true -> false
            else -> GpuInfo.detect().family == GpuInfo.Family.NOT_ADRENO
        }
        if (wanted && !serverBinary(context).isFile) {
            Log.w(TAG, "Venus wanted but ${serverBinary(context)} is missing")
            return false
        }
        return wanted
    }

    /**
     * Unpacks the guest ICD when the apk's copy differs from the one on disk and writes its
     * manifest. Returns the manifest, or null when the asset is missing or cannot be written.
     */
    fun installGuestDriver(context: Context): File? {
        val dir = dir(context).apply { mkdirs() }
        val library = File(dir, ICD_LIBRARY)
        return try {
            // Keyed on the apk install: an update may carry a new ICD, and the asset may be
            // compressed, so its size cannot be read without unpacking it anyway.
            val stamp = File(dir, "$ICD_LIBRARY.stamp")
            val installed = context.packageManager.getPackageInfo(context.packageName, 0).lastUpdateTime.toString()
            if (!library.isFile || FileUtils.readString(stamp) != installed) {
                val staging = File(dir, "$ICD_LIBRARY.part")
                context.assets.open("$ASSET_DIR/$ICD_LIBRARY").use { input ->
                    staging.outputStream().use { input.copyTo(it) }
                }
                if (!staging.renameTo(library)) throw java.io.IOException("rename $staging")
                stamp.writeText(installed)
                Log.i(TAG, "guest ICD unpacked: $library (${library.length()} bytes)")
            }
            val manifest = File(dir, MANIFEST)
            val json = """{"file_format_version":"1.0.1","ICD":{"library_path":"${library.absolutePath}","api_version":"1.4.0"}}"""
            if (FileUtils.readString(manifest) != json) manifest.writeText(json)
            manifest
        } catch (e: Exception) {
            Log.w(TAG, "could not install the guest ICD", e)
            null
        }
    }

    /** What the guest needs to reach the server: the Venus ICD's vtest transport and its socket. */
    fun guestEnvironment(context: Context): List<String> = listOf(
        "BL_VENUS=1",
        "VN_DEBUG=vtest",
        "VTEST_SOCKET_NAME=" + socket(context).absolutePath,
    )
}

/** The vtest server, started before the guest and stopped after it. */
class VenusServerComponent(private val logFile: File?) : SessionPart() {
    @Volatile private var pid = -1

    override fun start() {
        stop()
        val context = app()
        val binary = Venus.serverBinary(context)
        val socket = Venus.socket(context)
        socket.parentFile?.mkdirs()
        // A socket left by a session that did not end cleanly is not a listener; the server would
        // fail to bind onto it.
        socket.delete()
        val out = logFile?.let {
            try { PrintWriter(FileWriter(it, true)).apply { println("== Venus vtest server starting"); flush() } }
            catch (e: Exception) { Log.w(TAG, "could not open $it", e); null }
        }
        // --no-virgl: the build has no GL renderer, and asking for one fails every client.
        // No --multi-clients: the server forks a renderer per client (gamescope, the client, each
        // game), so one client's exit cannot take the others down - in one shared renderer a
        // disconnect aborted the whole server ("pthread_mutex_lock called on a destroyed mutex").
        // Clients share buffers as dma_buf fds, never by resource id, so nothing needs one process.
        val command = "${binary.absolutePath} --venus --no-virgl --socket-path ${socket.absolutePath}"
        pid = HostProcess.start(command, arrayOf("HOME=" + context.filesDir), context.filesDir, null) { line ->
            Log.i(TAG, line)
            if (out != null) synchronized(out) { out.println(line); out.flush() }
        }
        // The guest's first Vulkan call connects at once and does not retry: wait for the socket.
        val deadline = System.currentTimeMillis() + 3000L
        while (!socket.exists() && System.currentTimeMillis() < deadline) Thread.sleep(25L)
        Log.i(TAG, "started pid=$pid socket=$socket" + if (socket.exists()) "" else " (socket not there after 3 s)")
    }

    override fun stop() {
        if (pid != -1) {
            Process.killProcess(pid)
            pid = -1
        }
    }

    // Not offered for suspend: a stopped server would hang any guest call still in flight.

    private companion object {
        const val TAG = "VenusServer"
    }
}
