# euclid-cdk

The C++ SDK for a [euclid](https://github.com/jensvogt/euclid) server, alongside euclid-jdk (Java),
euclid-pdk (Python) and euclid-ndk (Node.js).

This first release covers **EAM** — euclid's access management module — which is where a login comes
from, and the two **request-signing schemes** a euclid client authenticates with. The other modules
(ESM, EQS, ENS, EKM, EKV, EAP, ESS, EAG) speak the same protocol through the same client and will
follow; until they do, `EAM::Session::NewRequest()` and `CDK::HttpClient` reach any action this SDK
does not name.

Both a **shared** and a **static** library are built: `libeuclid-cdk.so` and `libeuclid-cdk.a`.

## Using it

```cpp
#include <euclid/cdk/Euclid.h>

using namespace Euclid::CDK;

const auto session = EAM::Eam::ForServer("https://euclid.example.com")
                             .Credentials("jens", "secret")
                             .Login();

for (const auto page = session.ListUsers({.prefix = "j", .pageSize = 25}); const auto &user: page.items) {
    std::cout << user.userId << " " << user.email << "\n";
}
```

`Login()` reuses a still-valid session out of `~/.euclid/credentials`, so calling it repeatedly costs
nothing after the first time — and that file is shared with euclid-cli and the other SDKs, so a login
from any of them is picked up here. `UseCache(false)` forces a fresh login.

### Building against it

```cmake
find_package(euclid-cdk CONFIG REQUIRED)
target_link_libraries(my-app PRIVATE euclid::cdk)         # shared
target_link_libraries(my-app PRIVATE euclid::cdk-static)  # static
```

The same two names work when this project is added with `add_subdirectory()`.

## What it does

### Login and the session

`EAM::Eam` is the login builder; `EAM::Session` is what it answers with, and every EAM operation
hangs off that. The split exists because logging in and being logged in need different arguments.

```cpp
auto session = EAM::Eam::ForServer(url)
                       .Credentials("jens", "secret")
                       .Namespace("reports")             // applied after login, like eam login --namespace
                       .Scheme(SigningScheme::SigV4())   // RFC 9421 unless asked otherwise
                       .Auth(EAM::AuthMode::Signature)   // Auto, Signature or Bearer
                       .CaCertPath("/etc/euclid/euclid_cert.crt")
                       .Timeout(std::chrono::seconds(10))
                       .Login();
```

The session covers users, groups, accounts, namespaces, namespace grants, access keys and metrics.
The OIDC and SAML browser flows are the one part of EAM it does not wrap — those are redirects
through an identity provider rather than calls a library makes. `Session::Call()` sends any action
by name, both for those and for one that arrives in a later euclid than this SDK.

### Signing

A euclid client authenticates in one of two ways, and the server accepts either: a **bearer token**
from the login, or a **signature** made with the access key the login hands back.

| | `CDK::SigV4` | `CDK::HttpSignature` |
|---|---|---|
| Scheme | AWS Signature Version 4 | RFC 9421 HTTP Message Signatures |
| Presented in | `Authorization` | `Signature` / `Signature-Input` |
| Body bound through | `x-amz-content-sha256` | `Content-Digest` (RFC 9530) |
| Status | what euclid has always spoken | the standard replacement, and the default here |

Both are ports of the server's own `Core::SigV4` and `Core::HttpSignature`, kept line-for-line: what
a signature covers and how it is canonicalised is a wire format shared by the server, euclid-cli and
every SDK, and the two implementations have to agree byte for byte or nothing verifies. In
particular, `SigV4::SignedHeaderNames()` and `HttpSignature::CoveredComponents()` are fixed lists,
**order included** — neither is client-negotiated, so nothing in transit can narrow what a signature
actually covers and still have it verify.

Verification is here as well as signing. It is the only way to demonstrate that the two
canonicalisations are one canonicalisation, and a C++ service fronting euclid needs to check the
signatures it receives with the rules the server applies:

```cpp
if (const auto *scheme = SigningScheme::Of(request); scheme != nullptr) {
    const auto keyId = scheme->Verify(request, [](const std::string &id) { return secretOf(id); });
    if (!keyId.has_value()) return unauthorised();
}
```

Two notes worth carrying between implementations:

- **No `tag`.** euclid's server emits no RFC 9421 `tag` parameter and never looks at one, so a
  verifier that required a particular tag would reject the server's own signatures. One that arrives
  is accepted here and ignored.
- **`x-euclid-namespace` is not covered** by either scheme, knowingly and in every SDK. A namespace
  scopes what a request may touch, and the server checks it against the caller's grants rather than
  against the signature.

## Building

Dependencies are Boost (asio, beast, json, url) and OpenSSL — the same stack the euclid server
itself is built on. With vcpkg:

```bash
cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build -j
ctest --test-dir build --output-on-failure
```

| Option | Default | |
|---|---|---|
| `EUCLID_CDK_BUILD_SHARED` | `ON` | build `libeuclid-cdk.so` |
| `EUCLID_CDK_BUILD_STATIC` | `ON` | build `libeuclid-cdk.a` |
| `EUCLID_CDK_BUILD_TESTS` | top-level only | build and register the tests |
| `EUCLID_CDK_BUILD_EXAMPLES` | top-level only | build `eam-walkthrough` |
| `EUCLID_CDK_INSTALL` | top-level only | generate the install and package-config targets |

Both libraries are compiled from the same sources twice rather than shared through an object
library: the export macros differ between them, and on Windows a static library with
`__declspec(dllexport)` in it is not usable.

### Tests

The tests run a euclid server in-process (`tests/FakeGateway`) that verifies every signed request
with the same rules the real one applies, so a client that signs one thing and sends another fails
there rather than in production. They point `EUCLID_CREDENTIALS_FILE` at a scratch file for the
duration, and so never touch a real `~/.euclid/credentials`.

### Example

```bash
./build/examples/eam-walkthrough https://euclid.example.com jens secret [namespace]
```

Reads only, apart from the namespace it switches to when one is given.

## Licence

Apache-2.0. The euclid server itself is MPL-2.0; the SDKs are not, matching euclid-jdk, euclid-pdk
and euclid-ndk.
