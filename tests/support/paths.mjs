import {readFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {join} from 'node:path';
const root=fileURLToPath(new URL('../../',import.meta.url));
let built;
try{built=JSON.parse(readFileSync(join(root,'.local/build-info.json'),'utf8')).stage;}catch(error){if(error.code!=='ENOENT')throw error;}
export const stage=process.env.GROK_PET_STAGE??built;
export const jsonSource=process.env.CJSON_SOURCE_DIR??(stage?join(stage,'managed_components/espressif__cjson/cJSON'):'');
