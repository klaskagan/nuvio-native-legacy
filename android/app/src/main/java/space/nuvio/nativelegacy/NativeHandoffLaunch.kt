package space.nuvio.nativelegacy

import java.net.URI
import java.net.URLDecoder

internal object NativeHandoffLaunch {
    fun payload(url: String): String? = runCatching {
        require(url.length <= 65536)
        val uri = URI(url)
        require(uri.scheme in setOf("nuvio-legacy-handoff", "nuvio-legacy-handoff-debug"))
        require(uri.host == "handoff" && uri.userInfo == null && uri.port == -1 && uri.fragment == null && uri.path.isNullOrEmpty())
        val params = uri.rawQuery.orEmpty().split('&')
        require(params.size == 1 && params.single().startsWith("launch="))
        val decoded = URLDecoder.decode(params.single().substringAfter('='), "UTF-8")
        require(decoded.startsWith("{") && decoded.toByteArray(Charsets.UTF_8).size <= 16384)
        decoded
    }.getOrNull()
}
