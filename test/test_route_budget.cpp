// Reuse the real-controller fixture, but count discovery searches rather than
// timing a desktop CPU. The runner instruments a temporary Discovery.cpp copy.
#define main regressionMain
#include "test_main.cpp"
#undef main
namespace psu { extern unsigned routeSearches; }
int main() {
  try {
    for (uint8_t mode = 0; mode < 3; ++mode) {
      Rig r(installation(mode ? 9 : 2), false, false); r.temperature = 30;
      if (mode == 1) r.live = 0x101;
      r.step(40000); Terminal terminal(r); UiRig ui(r);
      terminal.input("alerts every 0\n"); terminal.drain(); r.can.incoming.clear();
      routeSearches = 0; r.c.tick(++r.now);
      const auto controller = routeSearches; CHECK(controller <= r.c.count());
      routeSearches = 0; terminal.console.tick(r.now); const auto serial = routeSearches;
      CHECK(serial == 0);
      UiFrame frame; routeSearches = 0; UiView::compose(frame, ui.ui, r.c, r.now);
      CHECK(routeSearches == 0);
      std::cout << "configured=" << unsigned(r.c.count()) << " live=" << unsigned(population(r.live))
          << " searches: controller=" << controller << " idle-serial=" << serial << " compose=" << routeSearches << '\n';
      // Presentation on a later millisecond may re-resolve each member once,
      // but hundreds of fresh status/metric queries must not redo those scans.
      ++r.now; routeSearches = 0;
      for (unsigned n = 0; n < 100; ++n) { r.c.controlStatus(r.now); r.c.availabilityAttention(r.now); }
      CHECK(routeSearches <= r.c.count());
      // DATA bursts do not change routing. The cache must be shared by all
      // frames in one receive batch, including the ninth configured member.
      routeSearches = 0;
      for (uint8_t n = 0; n < 32; ++n) r.can.incoming.push_back(data(r.c.count(), 0x7f, 30 * 1024));
      r.c.tick(++r.now); CHECK(r.can.incoming.empty() && routeSearches <= r.c.count());
      // Routine serial status sampling is paced; explicit diagnostics remain
      // available and never need more than one search per configured slot.
      r.now += console::statusIntervalMs; routeSearches = 0; terminal.drain(100);
      CHECK(routeSearches <= r.c.count());
      const auto count = routeSearches; terminal.drain(1000); CHECK(routeSearches == count);
    }
    std::cout << "PASS route search budget\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
