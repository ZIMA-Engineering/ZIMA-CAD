# Surface centroid in Body Measurement

When a measured history boundary has no solid volume but has positive surface
area, Body Measurement reports its surface-area-weighted centroid. This is the
centre of mass for uniform areal density, not a volume or mass estimate.

The existing OCCT surface integration supplies area and centroid during explicit
calculation/import. Both are stored in the native calculated packet. Rigid Body
placement, Mirror, Pattern and multi-Body aggregation transform or area-weight
the centre. Opening, previewing and saving the inspector consume these results
without invoking OCCT. Tessellation density does not define this centroid.

Positive-volume measurements retain their existing volume centroid and mass
inertia behavior. Missing density on a solid does not switch to a surface
centroid. Open surfaces do not acquire mass or mass inertia merely because a
material density is present. Mixed selections with positive solid volume retain
the existing solid-volume measurement semantics.

The inspector labels the fallback as a surface centroid in Czech, English,
German, French and Russian. Its preview/saved Origin follows the same inspection
and Save/OK/Cancel contract as volume measurements.
