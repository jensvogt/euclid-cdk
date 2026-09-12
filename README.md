# euclid-cdk

The C++ SDK for a [euclid](https://github.com/jensvogt/euclid) server, alongside euclid-jdk (Java),
euclid-pdk (Python) and euclid-ndk (Node.js).

It covers **EAM** — euclid's access management module — which is where a login comes from, the two
**request-signing schemes** a euclid client authenticates with, and three modules reached through
the session a login answers with: **ESM** (storage), **EQS** (queues) and **ENS** (notifications).
The other modules (EKM, EKV, EAP, ESS, EAG) speak the same protocol through the same client and will
follow; until they do, `CDK::ModuleClient` is what one is built out of, and
`EAM::Session::NewRequest()` and `CDK::HttpClient` reach any action this SDK does not name.

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

const ESM::Esm esm(session);
const auto bucket = esm.CreateBucket("reports");
esm.UploadFile(bucket.ern, "2026/q3.pdf", "q3.pdf");
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

### Storage

`ESM::Esm` is built from a session that has already logged in, and holds it rather than a copy of
what it knew at the time — so a `ChangeNamespace()` between two calls scopes the second one. It
covers buckets, objects, object attributes, subscriptions, encryption and the transfers.

```cpp
const ESM::Esm esm(session);

esm.PutObject(bucketErn, "notes/hello.txt", "written in one request\n",
              {.attributes = {{"author", "jens"}, {"revision", 1L}}});

esm.UploadFile(bucketErn, "data/large.bin", "large.bin", {.partSize = 8 * 1024 * 1024});
esm.DownloadFile(bucketErn, "data/large.bin", "copy.bin");
```

An object's attributes are typed: `COM::Variant` is a port of the server's own, so an attribute
stored as a `long` comes back as a `long` rather than as whatever JSON's one number type reads as.
A plain value is tagged with the type it is written as, which is why `1L` above is a long and `1`
would be an int; the two are different columns in euclid.

Four actions — `put-object`, `get-object`, `upload-part` and `download-part` — carry the object's
bytes themselves, with the bucket, the key and the part number riding as headers instead of in a
body, which is what keeps a 5 MiB part 5 MiB on the wire rather than a third larger as base64 inside
JSON. Those four also present the session's bearer token rather than a signature, which is what
euclid-cli and the other SDKs do for the same four, so every client writes objects the same way; a
session that asked for `AuthMode::Signature` signs them anyway.

`UploadFile()` and `DownloadFile()` send their parts from several threads and retry a step that
failed transiently, because a transfer is long and giving up on one part of it throws away
everything already transferred. A download is tried in one request first and falls back to the
multipart path when the server answers "too large", so a caller does not have to know which of the
two an object needs. Everything else is one request at a time, like the session.

### Queues and topics

`EQS::Eqs` and `ENS::Ens` are built from a session the same way, and are the two halves of euclid's
messaging: a queue holds a message until a consumer takes it, and a topic hands each message to
every subscriber and keeps it as a record of having done so.

```cpp
const EQS::Eqs eqs(session);

const auto queue = eqs.CreateQueue("orders");
eqs.SendMessage(queue.ern, R"({"order": 17})", {.attributes = {{"tenant", "acme"}}});

for (const auto &message: eqs.ReceiveMessages(queue.ern, {.waitTime = std::chrono::seconds(20)}).items) {
    handle(message.body);
    eqs.DeleteMessage(message.receiptHandle);     // after the work, not before
}
```

Receiving is a lease rather than a read: a message a consumer takes is invisible to every other
consumer until its visibility timeout expires, and deleting it with the receipt handle is what says
the work was done. A consumer that dies instead simply stops holding the lease, and the message
comes back.

`waitTime` is a long poll the **server** holds open, so an idle queue costs one request for the
whole window rather than one per tick, and a message arrives the instant it is sent. With no wait
asked for, the queue's depth is checked first and an empty queue costs no receive at all — a receive
is a write. The one case that loops is the server declining to wait, which it does when it has no
long-poll slot free; then the client waits a moment before asking again, since a server short of
threads does not need to be asked more often.

```cpp
const ENS::Ens ens(session);

const auto topic = ens.CreateTopic("order-events");
ens.Subscribe(topic.ern, queue.ern);              // every message from now on lands on the queue
ens.PublishMessage(topic.ern, R"({"order": 18})");
```

`StopTopic()` holds delivery without refusing publishers — what arrives meanwhile is kept and fanned
out when the topic is started again, which is what a subscriber being redeployed asks for — and
`SetTopicRetention()` says how long a published message is kept at all, since a topic is fanned out
at publish time and nothing else would ever remove it.

Two smaller things worth knowing. `Eqs::AsInternal()` is a view of the client whose requests are
marked as euclid's own traffic, for the polling that measures a system rather than uses it; without
it, instrumentation keeps a pool permanently awake and makes an idle module look busy.
And `PurgeAllQueues()` covers every namespace of the account while `PurgeAllTopics()` follows the
session's namespace — the two differ because each keeps the default it shipped with, in every SDK.

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

Behind it, `tests/FakeStorage` is a storage module small enough to read: an upload assembles its
parts in part order and a download hands back the byte range that was asked for. The part of a
multipart transfer a unit test cannot see is exactly the part that corrupts an object — a part
number off by one, a part size the two ends disagree about — so the transfers are checked against
something that actually reassembles what it is sent.

### Examples

```bash
./build/examples/eam-walkthrough       https://euclid.example.com jens secret [namespace]
./build/examples/esm-walkthrough       https://euclid.example.com jens secret [namespace]
./build/examples/messaging-walkthrough https://euclid.example.com jens secret [namespace]
```

The EAM one reads only, apart from the namespace it switches to when one is given. The other two
work in resources of their own, named after the moment they started, and delete them again at the
end — so a run that dies halfway leaves something obviously disposable behind rather than touching
anything of yours. The messaging one is both modules at once: a topic, a queue subscribed to it, and
a message that travels.

## Licence

Apache-2.0. The euclid server itself is MPL-2.0; the SDKs are not, matching euclid-jdk, euclid-pdk
and euclid-ndk.
