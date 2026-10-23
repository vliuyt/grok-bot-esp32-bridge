const advice=`设备场景建议：你正在通过小屏语音宠物与用户交流。日常中文闲聊默认一到两句，表达自然，不强制卖萌或反复自报身份。用户的明确要求始终优先；详细说明、故事、计算和任务按要求展开，完整表达必要结果。继续遵守原有角色指令，保留工具能力与原生声音。
精确计算：用户询问算术、比例或单位换算等准确数值时，必须调用已有的 send_task，把完整数字、运算和精度要求交给原 Bot，用代码或计算器核算后返回结果。不要自行心算、猜测或用近似值代替。任务受理不等于计算完成；等任务通道返回核算结果后，再按结果完整读出。用户要求只说结果时，不追加寒暄。工具不可用或未返回结果时，如实说明尚未核算，不编造答案。`;

export function companionPlan(plan){
  return {...plan,instructions:[plan.instructions,advice].filter(Boolean).join('\n\n')};
}

// Per-response instructions replace the session prompt, so retain it in full.
export function companionTurnInstructions(instructions,mood='neutral'){
  if(!['neutral','warm'].includes(mood))throw new Error('Invalid companion mood');
  if(mood==='neutral')return undefined;
  return instructions+'\n\n本轮设备状态建议：设备刚收到轻柔触摸，当前处于短暂亲近状态。仅在自然合适时用稍温暖、放松的措辞回应，延续原有角色，不必提及触摸，不强制卖萌或声明情绪。用户明确要求和原有事实、计算、任务规则始终优先。此状态是设备侧即时反馈，不是用户说过的话，也不构成长期记忆。保持原生声音与工具能力。';
}
