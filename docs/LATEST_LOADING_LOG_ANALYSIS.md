# Latest loading-screen log analysis

Observed device build identity: `d3b6488de470-ee73e597e8870339` (2026-11-23 22:36 log).

## Confirmed runtime facts

- IL2CPP and the offline Backend initialize successfully.
- Hook #5 `local_request` and Hook #3 `touch` both install successfully; Hook #3 is installed after Hook #5.
- Hook #3 is invoked twice. No `Hook #5 local_request entered` line appears, so installation is not evidence that the local request handler has received a callback.
- `artifact_capacity_tables` completes.
- The old diagnostic build observes an existing `Character_LevelFactor` root with 720 rows and a non-null dictionary, but `GrowthRecipeLoader.Root` is null. It does not execute the newer `character99_tables` XML reconstruction source.
- The old diagnostic build then forwards the original game `touch` because the custom prelude is incomplete. It has no evidence that the local protocol path was reached.
- No native crash appears in the supplied log; the last records are Hook #3 watchdog counters.

## Source changes after that device build

- `character99_tables` now constructs `Character_LevelFactorXmlRoot` and `GrowthRecipeXmlRoot`/row objects from the embedded 160 + 619 + 117 progression records, invokes the verified `FromXml` methods, merges LevelFactor through `ReplaceDataList`, and assigns GrowthRecipe through `GrowthRecipeLoader.set_Root`. It verifies list and dictionary state and tracks factor/recipe idempotence independently.
- Touch now calls step 2 `character99_tables`, step 3 `permanent_raid_tables`, logs that step 4 `seed_table_defaults` is not reconstructed (the reference `touch` treats a false return as non-fatal), and continues to step 5 `battle_multiple_tables`.
- Touch then stops at step 6 `custom_resource_tables`, which remains a mandatory, unimplemented helper. No lobby method is called before this mandatory prelude is complete.
- Hook #3 diagnostics now distinguish Hook #5 being installed from Hook #5 actually observing a callback.

## Verification boundary

The current source has passed the six Host CTest targets. This is not an Android device test. The next Android log must show the new build identity and `character99 substep completed` / `GrowthRecipe rows=736` before the managed reconstruction can be treated as device-verified. The loading screen is not yet claimed fixed because step 6 and later mandatory helpers remain incomplete.
