//******************************************************************
//
// Copyright 2015 Samsung Electronics All Rights Reserved.
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

#include "UnitTestHelperWithFakeOCPlatform.h"

#include "RequestHandler.h"
#include "RCSResourceObject.h"
#include "ResourceAttributesConverter.h"

#include "OCPlatform.h"

using namespace std;

using namespace OIC::Service;

constexpr char EXISTING[]{ "ext" };
constexpr int ORIGIN_VALUE{ 100 };

constexpr int NEW_VALUE{ 1 };

typedef OCStackResult (*RegisterResource)(OCResourceHandle&, std::string&,
        const std::string&, const std::string&, OC::EntityHandler, uint8_t);

TEST(RequestHandlerTest, DefaultHasNoCustomRepresntation)
{
    ASSERT_FALSE(RequestHandler().hasCustomRepresentation());
}

TEST(RequestHandlerTest, HasCustomRepresentationIfConstructedWithAttributes)
{
    ASSERT_TRUE(RequestHandler(RCSResourceAttributes{ }).hasCustomRepresentation());
}

TEST(RequestHandlerTest, CustomRepresentationContainsSameAttributesPassedToConstructor)
{
    RCSResourceAttributes attrs;
    attrs[EXISTING] = ORIGIN_VALUE;

    RequestHandler handler(attrs);

    auto converted = ResourceAttributesConverter::fromOCRepresentation(handler.getRepresentation());
    ASSERT_EQ(attrs, converted);
}

class SetRequestHandlerAcceptanceTest: public TestWithExternMock
{
public:
    RCSResourceObject::Ptr server;

    std::shared_ptr< SetRequestHandler > setRequestHandler;

    RCSResourceAttributes requestAttrs;

protected:
    void SetUp()
    {
        TestWithExternMock::SetUp();

        mocks.OnCall(
                mockFakePlatform, FakeOCPlatform::registerResource)
                        .Return(OC_STACK_OK);

        mocks.OnCall(
                mockFakePlatform, FakeOCPlatform::unregisterResource)
                        .Return(OC_STACK_OK);

        server = RCSResourceObject::Builder("a/test", "resourcetype", "").build();

        server->setAutoNotifyPolicy(RCSResourceObject::AutoNotifyPolicy::NEVER);
        server->setAttribute(EXISTING, ORIGIN_VALUE);

        setRequestHandler = make_shared< SetRequestHandler >();

        requestAttrs[EXISTING] = NEW_VALUE;
    }
};

TEST_F(SetRequestHandlerAcceptanceTest, NothingReplacedWithIgnoreMethod)
{
    auto replaced = setRequestHandler->applyAcceptanceMethod(
            RCSSetResponse::AcceptanceMethod::IGNORE, *server, requestAttrs);

    ASSERT_TRUE(replaced.empty());
}


TEST_F(SetRequestHandlerAcceptanceTest, NewValueApplyedWithAcceptMethod)
{
    setRequestHandler->applyAcceptanceMethod(
            RCSSetResponse::AcceptanceMethod::ACCEPT, *server, requestAttrs);

    ASSERT_EQ(NEW_VALUE, server->getAttribute<int>(EXISTING));
}

TEST_F(SetRequestHandlerAcceptanceTest, ReturnedAttrPairsHaveOldValue)
{
    auto replaced = setRequestHandler->applyAcceptanceMethod(
            RCSSetResponse::AcceptanceMethod::ACCEPT, *server, requestAttrs);

    ASSERT_EQ(ORIGIN_VALUE, replaced[0].second);
}

TEST_F(SetRequestHandlerAcceptanceTest, NothingHappenedWithEmptyAttrs)
{
    setRequestHandler->applyAcceptanceMethod(
            RCSSetResponse::AcceptanceMethod::ACCEPT, *server, RCSResourceAttributes{ });

    ASSERT_EQ(ORIGIN_VALUE, server->getAttribute<int>(EXISTING));
}

TEST_F(SetRequestHandlerAcceptanceTest, EverythingAppliedIfMethodIsAccept)
{
    requestAttrs[EXISTING] = "";

    auto replaced = setRequestHandler->applyAcceptanceMethod(
             RCSSetResponse::AcceptanceMethod::ACCEPT, *server, requestAttrs);

     ASSERT_EQ(ORIGIN_VALUE, replaced[0].second);
}


TEST_F(SetRequestHandlerAcceptanceTest, NoReplaceIfMethodIsDefaultAndTypeMismatch)
{
    requestAttrs[EXISTING] = "";

    auto replaced = setRequestHandler->applyAcceptanceMethod(
             RCSSetResponse::AcceptanceMethod::DEFAULT, *server, requestAttrs);

     ASSERT_TRUE(replaced.empty());
}

TEST_F(SetRequestHandlerAcceptanceTest, NoReplacefMethodIsDefaultAndRequestAttrsHasUnknownKey)
{
    constexpr char unknownKey[]{ "???" };

    requestAttrs[EXISTING] = ORIGIN_VALUE;
    requestAttrs[unknownKey] = ORIGIN_VALUE;


    auto replaced = setRequestHandler->applyAcceptanceMethod(
             RCSSetResponse::AcceptanceMethod::DEFAULT, *server, requestAttrs);

     ASSERT_TRUE(replaced.empty());
}
