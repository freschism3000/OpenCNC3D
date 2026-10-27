# Enhanced HUD raster oracle

The PNG fixtures are snapshots of the approved C&C Interface Lab v13 JavaScript compositor, captured. They are independent of the portable C game compositor. Never regenerate them from the C implementation to make a failure pass.

Run `python3 tools/sidebar_redesign/test/verify_enhanced.py` after baking `playable/hud640.pack`. It checks every shared frame and meter pixel, six sand/red construction-clock states, and all seven normal/engaged hover-lit buttons. The test compiles the actual portable C raster without SDL or OpenGL. Original HUD/card golden checks remain in the Lab verifier.

The original art's transparent corner cuts are part of the expected pixels. Cameo hit rectangles and grid layout use native integer pixels; compact displays crop complete rows and keep the authored lower cap instead of scaling artwork.
