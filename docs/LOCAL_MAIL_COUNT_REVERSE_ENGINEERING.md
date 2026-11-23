# local_mail_count reverse engineering

Reference function: `local_mail_count@0x243928` in the original ELF. The replacement does not load or link the original shared object.

## Recovered behavior

1. Read the Store document's `mails` array. If the property is absent, the effective count is zero.
2. Iterate each entry and query the exact JSON key `Receipt`.
3. Ignore missing/non-numeric `Receipt` values. The native code uses cJSON's 32-bit `valueint` field and increments the counter only when `valueint > 1`. The replacement therefore truncates numeric values toward zero and clamps to signed 32-bit bounds before comparing; it does not compare the original floating-point value directly.
4. Read the Account mail-state object at `Account + 0x60` and call `SetMailCount(count, count > 0)`.
5. The method is invoked through IL2CPP reflection only after the exact signature `(System.Int32, System.Boolean) -> System.Void` is verified. Null state, missing method, ABI mismatch, or managed exception returns failure instead of attempting an unsafe call.

The function does not mutate the Store mail array or claim/delete mail. It only updates the Account's mail count state.

## Replacement files

- `src/local_mail_count.hpp/.cpp`: count reducer and guarded IL2CPP invocation.
- `tests/local_mail_count_test.cpp`: absent array, wrong field types, integer boundary semantics, and floating-point truncation behavior.

## Verification boundary

Host tests validate the JSON predicate. They do not prove the target Android build's `Account + 0x60` layout or live `SetMailCount` reflection signature; those require an Android IL2CPP runtime log. This helper is not yet sequenced into `bootstrap(Account)` or `touch()`.
