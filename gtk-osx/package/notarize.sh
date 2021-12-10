#!/bin/bash

HEX="[0-9a-f"]
UUID_REGEX="($HEX{8}-$HEX{4}-$HEX{4}-$HEX{4}-$HEX{12})"
DMG_FILE=`find /tmp/artifacts/ -name "gimp-2.10*.dmg"`

# Submit for notarization and get our ticket ID if submission is successful
ALTOOL_OUT="$(xcrun altool --notarize-app --file ${DMG_FILE} -u ${notarization_login} --primary-bundle-id org.gimp.gimp-2.10 -p ${notarization_password} 2>&1)" || ALTOOL_FAILED=true

echo "$ALTOOL_OUT"

if [[ $ALTOOL_FAILED ]]; then
  # In case the error is "The software asset has already been uploaded",
  # we can still find the UUID prefix and continue.
  UUID_PREFIX="The upload ID is "
else
  UUID_PREFIX="RequestUUID[[:space:]]*=[[:space:]]*"
fi

if [[ $ALTOOL_OUT =~ $UUID_PREFIX($UUID_REGEX) ]]; then
  REQUEST_UUID=${BASH_REMATCH[1]}
else
  echo "Failed finding Request UUID in altool output"
  exit 1
fi

echo "$REQUEST_UUID"

# Now we loop around, waiting for the notarization request to complete so we can check its status
for run in {1..360}; do
  sleep 60
  NOT_INFO="$(xcrun altool --notarization-info $REQUEST_UUID -u ${notarization_login} -p ${notarization_password}  2>&1)" || echo "$NOT_INFO"
  NOT_STATUS=$(echo "$NOT_INFO" | grep Status: | awk -F ": " '{print $NF}')
  NOT_LOGURL=$(echo "$NOT_INFO" | grep LogFileURL: | awk -F ": " '{print $NF}')
  if [[ "$NOT_STATUS" == "in progress" ]]; then
     echo "Notarization in progress. Waiting 60 seconds before trying again."
     continue
  elif [[ "$NOT_STATUS" == "invalid" ]]; then
     echo "Notarization failed with status invalid. Showing log"
     curl "$NOT_LOGURL"
     exit 1
  elif [[ "$NOT_STATUS" == "success" ]]; then
     echo "Notarization succeeded. Showing log"
     curl "$NOT_LOGURL"
     break
  elif [[ "$NOT_INFO" == *"Could not find the RequestUUID"* ]]; then
     echo "Waiting 60 seconds before trying again..."
     continue
  else
     echo "Error: Unknown notarization status $NOT_STATUS."
     exit 1
  fi
done

if [[ "$NOT_STATUS" == "in progress" ]]; then
  echo "Error: Notarization timed out."
  exit 1
fi

xcrun stapler staple -v ${DMG_FILE}

