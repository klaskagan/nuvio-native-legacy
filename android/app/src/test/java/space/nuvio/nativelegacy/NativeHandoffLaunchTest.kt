package space.nuvio.nativelegacy
import org.junit.Assert.*
import org.junit.Test
import java.net.URLEncoder

class NativeHandoffLaunchTest {
    private val payload = "{\"token\":\"a+b/\"}"
    private fun url(scheme: String = "nuvio-legacy-handoff-debug") = "$scheme://handoff?launch=" + URLEncoder.encode(payload, "UTF-8")
    @Test fun decodesProductionAndDebugRoutesWithoutLosingPlusCharacters() {
        assertEquals(payload, NativeHandoffLaunch.payload(url()))
        assertEquals(payload, NativeHandoffLaunch.payload(url("nuvio-legacy-handoff")))
    }
    @Test fun rejectsAmbiguousAndMalformedRoutes() {
        for (bad in listOf(url()+"&launch=x", url()+"#fragment", "nuvio-legacy-handoff:opaque", "nuvio-legacy-handoff://other?launch=%7B%7D", "nuvio-legacy-handoff://handoff?launch=%zz", url("https")))
            assertNull(bad, NativeHandoffLaunch.payload(bad))
    }
    @Test fun rejectsOversizedPayloadsBeforeNativeDelivery() {
        assertNull(NativeHandoffLaunch.payload("nuvio-legacy-handoff://handoff?launch=" + URLEncoder.encode("{"+"x".repeat(16384), "UTF-8")))
        assertNull(NativeHandoffLaunch.payload("x".repeat(65537)))
    }
}
