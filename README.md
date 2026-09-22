# euclid-cdk

The C++ SDK for a [euclid](https://github.com/jensvogt/euclid) server, alongside euclid-jdk (Java),
euclid-pdk (Python) and euclid-ndk (Node.js).

It covers **EAM** — euclid's access management module — which is where a login comes from, the two
**request-signing schemes** a euclid client authenticates with, and every module reached through the
session a login answers with: **ESM** (storage), **EQS** (queues), **ENS** (notifications), **EKM**
(keys and certificates), **EKV** (tables of items), **ESS** (secrets), **ETS** (the FTP and SFTP
endpoints onto a bucket), **EMO** (monitoring, including a metrics registry an application records
into), **EAP** (the applications euclid runs) and **EAG** (the API gateway that publishes them).

That is all of euclid. For an action this SDK does not name — one newer than the release you have —
`CDK::ModuleClient` is what a module client is built out of, and `EAM::Session::NewRequest()` and
`CDK::HttpClient` reach any action at all.

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
at publish time and nothing else would ever remove it. Its two named values are
`ENS::InstallationRetention` (0), which follows `euclid.modules.ens.retention-period` as that
changes, and `ENS::RetentionForever` (-1), which is not a very long period but the absence of one —
the server stores such a message with no expiry, which is what its TTL index ignores, so the topic
grows without limit and only `PurgeTopic()` empties it.

Three things a queue or a topic is created with can be changed afterwards rather than only at
creation: `Eqs::SetQueueDelay()`, `Eqs::SetQueueMaxMessageLength()` and
`Ens::SetTopicMaxMessageLength()`. Each applies to what is sent from then on — a message already
waiting keeps the release time it was given, and one already stored was measured against the limit
in force when it arrived. The two length limits read the same zero differently, which is the
server's rule rather than a slip: a queue with `EQS::InstallationMaxMessageLength` has no limit of
its own and is measured against the installation's figure, while a topic that accepts nothing is
refused outright — holding a topic for a while is what `StopTopic()` is for, and it is reversible.

Two smaller things worth knowing. `Eqs::AsInternal()` is a view of the client whose requests are
marked as euclid's own traffic, for the polling that measures a system rather than uses it; without
it, instrumentation keeps a pool permanently awake and makes an idle module look busy.
And `PurgeAllQueues()` covers every namespace of the account while `PurgeAllTopics()` follows the
session's namespace — the two differ because each keeps the default it shipped with, in every SDK.

### Keys and certificates

`EKM::Ekm` holds encryption keys and the certificates a deployment serves. Key material never leaves
the server: encrypting sends the bytes to the key rather than fetching the key to the bytes.

```cpp
const EKM::Ekm ekm(session);

const auto key = ekm.CreateKey({.description = "customer exports"});
const auto sealed = ekm.Encrypt(key.name, "account 4711");
const auto plain  = ekm.Decrypt(key.name, sealed);
```

A key is named two ways and they are not interchangeable: `name` is the ID the server minted, and is
what encrypts, decrypts and is deleted; the ERN is what revokes, describes and tags. Both are on
every `Key` a listing returns.

`DeleteKey()` schedules rather than deletes — everything the key encrypted becomes unreadable when
the date passes, and the window is the only chance anybody gets to notice. `RevokeKey()` is the one
to reach for when a key should no longer be used but the data under it is still wanted: a revoked
key stops encrypting and goes on decrypting. There is no `Metrics()` here, unlike every other
module: EKM's server answers `get-metrics` with a 404, so `Call()` is where that would live.

### Tables

`EKV::Ekv` is a key-value store: tables of items, read by key rather than searched.

```cpp
const EKV::Ekv ekv(session);
ekv.CreateTable("sessions", "userId", {.sortKey = "startedAt", .sortKeyType = std::string(EKV::KeyNumber)});

ekv.PutItem("sessions", {{"userId", "jens"}, {"startedAt", 1757462400}, {"host", "laptop"}});
const auto recent = ekv.Query("sessions", "jens", {.sortOperator = std::string(EKV::SortGe),
                                                   .sortValue = 1757462400});
```

A table is keyed on one attribute or two: a partition key that identifies an item, and optionally a
sort key that orders the items sharing one — which is what makes a partition readable as a range.
Only the keys have declared types, and the type is what a comparison is made under: a `KeyNumber`
sort key orders 2, 9, 10, 100 rather than putting `"10"` before `"9"`.

Everything else about an item is free-form JSON — `boost::json::object` in and out, not the tagged
`COM::Variant` that EQS, ENS and ESM attributes use, because EKV stores documents rather than typed
attribute maps. `Item::attributes` is what to pass straight back to `PutItem()`: the server keeps an
item's timestamps among its attributes as `_created` and `_modified`, and this SDK lifts them out —
a write replaces rather than merges, so leaving them in would make an item that was read, changed
and written back grow two attributes it never had.

`GetItem()` throws on a miss and `FindItem()` answers an empty `std::optional` instead; only a 404
becomes empty, since a refusal or a malformed key does not mean "not there". `Query()` addresses a
partition, which is what EKV is for; `Scan()` reads the table, which is for an export rather than a
lookup.

### Secrets

`ESS::Ess` keeps values encrypted under an EKM key and hands them back one at a time.

```cpp
const ESS::Ess ess(session);

ess.CreateSecret("db-password", "hunter2", {.description = "the reporting database"});
const auto password = ess.GetSecret("db-password").value;
```

`GetSecret()` is the only call that answers with a value — listing, rotating and tagging all answer
with metadata alone, so those can be logged and printed without being the thing that leaks it. The
value's life is tied to the key's: deleting that key in EKM is what makes the value unrecoverable,
whatever ESS still says about the secret.

`UpdateSecret()` takes `std::optional` fields for the same reason `ETS::Ets::UpdateServer()` does —
an unset description leaves the stored one alone, an empty one clears it. A value is the exception:
the server refuses an empty one, so this does too, one round trip earlier. Naming a `keyErn` moves
the secret onto that key, which is how one is taken off a key being retired; that is not a rotation,
and only a new value bumps the version.

### Applications

`EAP::Eap` is what euclid runs, from what, and as whom.

```cpp
const EAP::Eap eap(session);

eap.CreateApplication("order-service", std::string(EAP::RuntimeBinary), "artifacts", "order-service-1.4.0",
                      {.queues = {"orders"}, .maxInstances = 4});
eap.StartApplication("order-service");
```

An application is deployed from an artifact already in a bucket — ESM puts it there, EAP names it.
The deployment says which buckets and queues it may reach, and euclid grants those to the identity
it runs as: a technical principal it creates for the application unless one is named, with no
password, no login and one access key. Nothing an application leaks is then a person's credential.

Starting is a desired state rather than a wait, as in ETS, so the application in the answer is
usually still `STOPPED`. `RestartApplication()` exists instead of a stop followed by a start because
between those two the desired state is stopped, and a caller that fails in between leaves the
application down.

`UpdateApplication()` takes `std::optional` fields — an unset command leaves the stored one alone,
an empty one hands the artifact back to the runtime's own interpreter. Two things there are worth
knowing: `buckets` and `queues` are re-resolved together, so naming one and not the other revokes
what the other granted (name both, or neither); and `nameSpace` is a *move* rather than a field
change, which is why unset and empty differ — empty moves the application back to the account root.

**`ReportLoad()` is the one call here an application makes about itself** rather than an
administrator making it about the application:

```cpp
// on a schedule, while the application works
eap.ReportLoad("order-service", {.utilisation = 42.5, .backlog = queue.size(), .active = inFlight});
```

The instance id comes from `EUCLID_INSTANCE_ID`, which euclid's manager sets for every process it
starts, so an application need not carry it around; `EAP::Eap::InstanceId()` reads it. The
application id is always sent, even though the server can infer it from a caller named
`app-<something>` — that inference only holds while an application's id and the identity it runs as
agree, and when they didn't, every report went to a pool that did not exist, answered `200`, and
left the autoscaler blind. Everything else in this module is administrator-only.

### The API gateway

`EAG::Eag` publishes a path to the outside world and says what answers it. The gateway routes by
configuration rather than by convention, so the name of whatever serves a path never appears in the
URL a caller asks for — which is what lets an application be renamed, replaced or split across
several routes without anything calling it having to change.

```cpp
const EAG::Eag eag(session);

eag.CreateRoute("orders", "/orders", "order-service",
                {.methods = {std::string(EAG::MethodGet)}, .authentication = std::string(EAG::AuthEuclid)});
```

A route names exactly one of three things, and which one is its whole character — so there is a call
for each rather than one call with fields that contradict each other:

| call | what answers the path |
|---|---|
| `CreateRoute()` | an application EAP runs |
| `CreateModuleRoute()` | euclid itself — `("login", "/euclid/login", "eam", "login")` is how a browser logs in without a second origin and CORS between them |
| `CreateUploadRoute()` | a bucket: the body is streamed into ESM and nothing is forwarded |

Paths match as a prefix, because a REST resource is a tree, and the longer of two matching routes
wins — so a specific route can be carved out of a general one later without either being rewritten.
Two routes may share a path when their methods don't overlap, which is how reads and writes of one
resource go to different applications; an overlap is refused with `409` rather than decided by sort
order.

One action per module route, deliberately: a route for `/euclid` that passed its remaining segments
through as actions would publish every action the module has, including the ones that delete users.

`UpdateRoute()` takes `std::optional` fields, and `SetRouteActive()` is how something exposed by
mistake is withdrawn in a hurry and put back knowing it returns exactly as it was — an inactive route
still holds its path, so nothing else can claim it meanwhile. `ListListeners()` reports the ports
themselves: they are written in the installation's configuration rather than created here, so what it
answers is what the gateway is *doing* — `serving` false with listeners configured is a port that was
taken or a certificate that could not be loaded.

Two things this client does by leaving fields out rather than sending them: an unnamed namespace,
region or authentication is **absent** from the request, because the server falls back to its default
only for a field that is not there at all — an empty namespace would publish the route at the account
root instead of in the session's. And an upload route defaults to `AuthEuclid` where a proxy route
defaults to none, because an unauthenticated upload route is a public write endpoint into somebody's
bucket. Every action here is administrator-only.

### Transfer servers

`ETS::Ets` owns the FTP and SFTP endpoints onto a bucket — which protocol, which port, who may log
in, which bucket the files really live in. It never speaks either protocol itself; euclid's manager
turns a definition into a running process, and the process reads its definition back from here.

```cpp
const ETS::Ets ets(session);

ets.CreateServer("partner-drop", "invoices", 2222, {.userGroups = {"partners"}, .homeDirectory = "incoming/"});
ets.StartServer("partner-drop");
```

So a file uploaded over FTP is an object in a bucket, with the events and the lifecycle every other
object has. `desiredState` is what was asked for and `state` is what is observed running: the two
differing is a server starting up, and differing for long is one that cannot. Starting one only
writes the desired state, so the server in the answer is usually still `STOPPED`.

`UpdateServer()` takes `std::optional` fields because the server distinguishes a field being *sent*
from one that is not, rather than one value from another — an unset field leaves the stored one
alone, and an empty string replaces it. Every action here is administrator-only.

### Application monitoring

`EMO::Emo` is the monitoring module's client — `PushMetrics()`, and the administrator-only `List()`
and `Average()` for reading rows back. But an application does not want to push final numbers; it
wants to count things and time them and have something else do the arithmetic. euclid-jdk gets that
from Micrometer, and C++ has no Micrometer.

It does not need one. What Micrometer provides is two separable things — meters an application
records into, and a registry that accumulates them over a step and publishes the result — and
euclid's own C++ modules have done both for years with `Core::Monitoring`. `EMO::MeterRegistry` is
that, pushed through an authenticated session instead of a Unix socket:

```cpp
const EMO::Emo emo(session);
EMO::MeterRegistry metrics(emo, {.module = "invoice-parser", .commonLabels = {{"host", hostname}}});

const auto parsed   = metrics.CounterOf("invoices.parsed");
const auto rejected = metrics.CounterOf("invoices.parsed", {{"outcome", "rejected"}});
const auto duration = metrics.TimerOf("invoice.parse");
metrics.GaugeFrom("queue.depth", [&queue] { return static_cast<double>(queue.size()); });

for (const auto &invoice: incoming) {
    const EMO::ScopedTimer measure(duration);          // recorded however the scope is left
    Parse(invoice) ? parsed.Increment() : rejected.Increment();
}
```

A thread publishes every step (a minute by default) and nothing else has to be arranged; a step of
zero starts no thread and leaves `Publish()` to the application's own loop. What lands in EMO lands
in the same rows its own collectors write, and so in the same rollups, retention and graphs as CPU,
memory and the module gauges.

Three things are worth knowing, because each is a way a graph could otherwise lie:

- **A counter reports the step and starts again; a gauge reports what it reads and does not.** That
  is what makes a counter a *rate* to EMO, which sums it on rollup, and a gauge a *gauge*, which
  averages it. A counter pushed as a gauge is averaged into nonsense.
- **A timer becomes three series** — `name.count` and `name.total` as rates, `name.max` as a gauge,
  in milliseconds — under the same names euclid-jdk's Micrometer registry uses, so a C++
  application's timings graph beside a Java one's. There are no percentiles, because EMO stores one
  value per series per interval; a p99 would have to be a series of its own.
- **Labels are what split one series into several**, and every combination is a stored row per step
  forever. Decide them where the meter is created — a label carrying a request id is how a
  monitoring database fills up.

A push that fails is counted in `FailedPublishes()` and dropped rather than retried: a rate sent
twice is counted twice, and nothing here throws at the application, which does not stop because it
could not say how it was doing.

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
./build/examples/eam-walkthrough        https://euclid.example.com jens secret [namespace]
./build/examples/esm-walkthrough        https://euclid.example.com jens secret [namespace]
./build/examples/messaging-walkthrough  https://euclid.example.com jens secret [namespace]
./build/examples/monitoring-walkthrough https://euclid.example.com jens secret [namespace]
```

The EAM one reads only, apart from the namespace it switches to when one is given. The storage and
messaging ones work in resources of their own, named after the moment they started, and delete them
again at the end — so a run that dies halfway leaves something obviously disposable behind rather
than touching anything of yours. The messaging one is both modules at once: a topic, a queue
subscribed to it, and a message that travels.

The monitoring one records and publishes a run of made-up work, then reads the rows back if the
login may. It is the one that cannot clean up after itself — there is no delete for a metric, and
they go when EMO's retention takes them — so it reports under a module name of its own rather than
into anything you would recognise. It is also the clearest demonstration of what a step is: it
prints what the run counted beside what the meters still hold, and the two differ by exactly what
the publisher already took.

## Licence

Apache-2.0. The euclid server itself is MPL-2.0; the SDKs are not, matching euclid-jdk, euclid-pdk
and euclid-ndk.
