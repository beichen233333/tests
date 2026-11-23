# Latest loading-screen log analysis

Observed device build identity: `8c59286dcac5-f2d28a70cd4c6832` (2026-11-24 00:38 log).

## Confirmed runtime facts

- IL2CPP initialized successfully (174 assemblies, 52 required exports resolved); ManagedWire loaded.
- Compatibility Store was opened before Backend migration.
- Backend imported the historical compatibility save over a recognized synthetic placeholder. It preserved the original compat file, archived the placeholder, restored 30 characters and 131 inventory entries, and preserved tutorial bits `725367` and initial index `11`. Diagnostics reported `legacySaveImported=true` and `storeRecovery=compat-store-imported-over-placeholder`.
- Hook #5 and Hook #3 both installed successfully. Hook #3 was installed after Hook #5. No `Hook #5 local_request entered` line appears, so Hook #5 has not yet been proven to receive this login request.
- On tap, Controller, DataManager and Account were recovered and validated. Artifact capacity step 1 completed.
- Character99 step 2 was blocked by the prior build's safety policy before XML allocation; it did not crash in this log. The GrowthRecipe loader had a null Root, so step 2 must reconstruct it.
- Hook #3 forwarded the tap to the original game `touch()` because the custom prelude was incomplete. The log therefore does not prove an offline login request/response cycle.

## Source fix following this log

- IL2CPP `runtime_invoke` reference parameters must receive the managed object pointer itself; the previous helper passed the address of the native pointer variable. Value types still receive the address of their native storage.
- Character99 setters are now split into value/reference paths. Value setters validate IL2CPP value-type metadata and storage width. Reference setters validate reference-type metadata and compare the actual object class with the declared parameter class.
- XML Root `DataList` assignment is verified by pointer equality, expected array length, array element class, and first/last row runtime classes before `FromXml` is called.
- The previous compile-time FromXml block is removed. The new code writes a detailed invocation-start record only after the XML payload and method signature pass validation.

## Verification boundary

The current Host build and all 9 CTest cases pass. The latest code has not yet been built with Android NDK or tested in the game process. A successful next device log should include:

1. `character99 XML payload validated before FromXml` for both roots.
2. `character99 FromXml invocation starting with validated managed references`.
3. `Character_LevelFactorRoot.FromXml` / `GrowthRecipeRoot.FromXml` return and post-call row/dictionary verification.
4. A subsequent `Hook #5 local_request entered` when the game's login flow issues a locally handled request.

If FromXml still faults, the new invocation-start pointers and tombstone will isolate the failing root. Until Hook #5 receives and dispatches a request, and later mandatory touch-prelude steps (starting at `custom_resource_tables`) are restored, the full offline lobby is not yet complete.
